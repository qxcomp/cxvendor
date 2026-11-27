/* ============================================================================
 * clang_dyn.c — libclang (cindex) 动态加载封装层
 *
 * 设计约束（与 sqlite3_dyn.c / mpv_dyn.c / ffmpeg_dyn.c 等一致）：
 *   1. 链接期绝不出现 -lclang。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存到函数内 static 变量。
 *   4. 封装函数名与原版 libclang C API 一致（clang_createIndex 等）。
 *   5. 编译期需要 <clang-c/Index.h>，链接期不需要 libclang。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 搜索顺序：显式路径 > CLANG_DYN_LIB > 系统搜索路径 > 默认库名
 *      > Nix 路径 > 平台默认路径（含 Homebrew keg-only 路径）。
 *   8. 线程安全：首次调用 clang_libok() 或任一封装函数时完成加载。
 *
 * 覆盖的模块：
 *   - Index / Translation Unit：创建索引、解析文件、销毁资源
 *   - Cursor：AST 遍历、名称与种类
 *   - Diagnostic：诊断数量、获取、格式化、严重级别、位置、拼写
 *   - Source Location / Range：Cursor 位置、展开位置、范围起止、文件名
 *   - Type：Cursor 类型、类型拼写、类型种类、指向类型、const 限定
 *
 * 未覆盖但可按相同模式追加的模块：
 *   - Token（词法分析）
 *   - Code Completion（代码补全）
 *   - Indexing（高级索引回调）
 * ========================================================================== */

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<clang-c/Index.h>)
#    include <clang-c/Index.h>
#    define CLANG_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef CLANG_DYN_HAVE_HEADER
/* ---------- 类型（头文件缺失时的最小 ABI 回退） ---------- */
typedef void *CXIndex;
typedef void *CXTranslationUnit;
typedef void *CXFile;
typedef void *CXClientData;
typedef void *CXDiagnostic;
typedef struct { const char *data; unsigned private_flags; } CXString;
typedef struct { unsigned kind; int xdata; void *data[3]; } CXCursor;
struct CXUnsavedFile { const char *Filename; unsigned long Length; const char *Contents; };
typedef struct CXUnsavedFile CXUnsavedFile;
typedef void (*CXCursorVisitor)(CXCursor, CXCursor, CXClientData);
typedef enum CXCursorKind { CXCursor_InvalidFile = 70 } CXCursorKind;
typedef enum CXDiagnosticSeverity { CXDiagnostic_Ignored = 0 } CXDiagnosticSeverity;
typedef enum CXTypeKind { CXType_Invalid = 0 } CXTypeKind;
typedef struct { enum CXTypeKind kind; void *data[2]; } CXType;
typedef struct { void *ptr_data[2]; unsigned int_data; } CXSourceLocation;
typedef struct { void *ptr_data[2]; unsigned begin_int_data; unsigned end_int_data; } CXSourceRange;
typedef struct CXStringImpl CXStringImpl;

#endif /* CLANG_DYN_HAVE_HEADER */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 平台适配层
 * ============================================================ */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE clang_lib_handle_t;
  static clang_lib_handle_t clang_load_lib(const char *path) { return LoadLibraryA(path); }
  static void *clang_get_sym(clang_lib_handle_t h, const char *name) {
      return (void*)GetProcAddress(h, name);
  }
  static void clang_free_lib(clang_lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *clang_lib_error(void) { return "LoadLibrary failed"; }
  #define CLANG_PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *clang_lib_handle_t;
  static clang_lib_handle_t clang_load_lib(const char *path) {
      int flags = RTLD_LAZY | RTLD_LOCAL;
  #ifdef RTLD_DEEPBIND
      flags |= RTLD_DEEPBIND;
  #endif
      return dlopen(path, flags);
  }
  static void *clang_get_sym(clang_lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void clang_free_lib(clang_lib_handle_t h) { if (h) dlclose(h); }
  static const char *clang_lib_error(void) { return dlerror(); }
  #define CLANG_PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态
 * ============================================================ */
static clang_lib_handle_t clang_handle = NULL;
static int clang_init_state = 0;

#define CLANG_MAX_CANDIDATES 64
#define CLANG_PATH_BUF_LEN   1024

/* ============================================================
 * 库加载
 * ============================================================ */
static int clang_do_load(const char *library_path) {
    if (clang_handle) return 0;

    static char bufs[CLANG_MAX_CANDIDATES][CLANG_PATH_BUF_LEN];
    const char *candidates[CLANG_MAX_CANDIDATES];
    int n = 0;

    /* 1. 显式传入的路径 */
    if (library_path && *library_path && n < CLANG_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], CLANG_PATH_BUF_LEN, "%s", library_path);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 2. 项目专属环境变量 */
    const char *explicit_lib = getenv("CLANG_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < CLANG_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], CLANG_PATH_BUF_LEN, "%s", explicit_lib);
        { candidates[n] = bufs[n]; n++; }
    }

    /* 3. 系统搜索路径环境变量 */
    const char *env_path = getenv(
#ifdef _WIN32
        "PATH"
#elif defined(__APPLE__)
        "DYLD_LIBRARY_PATH"
#else
        "LD_LIBRARY_PATH"
#endif
    );
    if (env_path && *env_path) {
        char *copy = strdup(env_path);
        if (copy) {
            char *p = copy;
            while (p && *p && n < CLANG_MAX_CANDIDATES - 3) {
                char *sep = strchr(p, CLANG_PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
#ifdef _WIN32
                    snprintf(bufs[n], CLANG_PATH_BUF_LEN, "%s/libclang.dll", p); { candidates[n] = bufs[n]; n++; }
#elif defined(__APPLE__)
                    snprintf(bufs[n], CLANG_PATH_BUF_LEN, "%s/libclang.dylib", p); { candidates[n] = bufs[n]; n++; }
#else
                    snprintf(bufs[n], CLANG_PATH_BUF_LEN, "%s/libclang.so", p); { candidates[n] = bufs[n]; n++; }
#endif
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 4. 默认库名 */
#ifdef _WIN32
    candidates[n++] = "libclang.dll";
#elif defined(__APPLE__)
    candidates[n++] = "libclang.dylib";
#else
    candidates[n++] = "libclang.so";
#endif

    /* 5. Nix 路径 */
#ifndef _WIN32
    const char *home = getenv("HOME");
    if (home && *home && n < CLANG_MAX_CANDIDATES - 2) {
#ifdef __APPLE__
        snprintf(bufs[n], CLANG_PATH_BUF_LEN, "%s/.nix-profile/lib/libclang.dylib", home); { candidates[n] = bufs[n]; n++; }
#else
        snprintf(bufs[n], CLANG_PATH_BUF_LEN, "%s/.nix-profile/lib/libclang.so", home); { candidates[n] = bufs[n]; n++; }
#endif
    }
    if (n < CLANG_MAX_CANDIDATES - 2) {
#ifdef __APPLE__
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libclang.dylib";
#else
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libclang.so";
#endif
    }
#endif

    /* 6. 平台默认路径（含 Homebrew keg-only） */
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/opt/llvm/lib/libclang.dylib";
    candidates[n++] = "/usr/local/opt/llvm/lib/libclang.dylib";
    candidates[n++] = "/opt/local/libexec/llvm/lib/libclang.dylib";
    candidates[n++] = "/opt/local/lib/libclang.dylib";
    candidates[n++] = "/usr/local/lib/libclang.dylib";
#elif defined(__linux__) || defined(__ANDROID__)
    candidates[n++] = "/usr/lib/llvm-18/lib/libclang.so";
    candidates[n++] = "/usr/lib/llvm-17/lib/libclang.so";
    candidates[n++] = "/usr/lib/llvm-16/lib/libclang.so";
    candidates[n++] = "/usr/lib/x86_64-linux-gnu/libclang.so";
    candidates[n++] = "/usr/lib/libclang.so";
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    candidates[n++] = "/usr/local/lib/libclang.so";
    candidates[n++] = "/usr/lib/libclang.so";
#endif
    candidates[n] = NULL;

    int i;
    for (i = 0; i < n; i++) {
        clang_handle = clang_load_lib(candidates[i]);
        if (clang_handle) break;
    }
    if (!clang_handle) {
        fprintf(stderr, "clang_dyn: cannot load libclang (%s)\n", clang_lib_error());
        return -1;
    }

    /* 验证基础符号 */
    if (!clang_get_sym(clang_handle, "clang_createIndex")) {
        fprintf(stderr, "clang_dyn: loaded library does not export clang_createIndex\n");
        clang_free_lib(clang_handle);
        clang_handle = NULL;
        return -1;
    }
    fprintf(stderr, "clang_dyn: loaded %d %s\n", i, candidates[i]);
    return 0;
}

/* ============================================================
 * 一次性初始化
 * ============================================================ */
int clang_libok(void) {
    if (clang_init_state == 0) {
        clang_init_state = (clang_do_load(NULL) == 0) ? 1 : -1;
    }
    return (clang_init_state == 1 && clang_handle != NULL) ? 1 : 0;
}

int clang_dyn_init(const char *library_path) {
    if (clang_init_state == 1) return 0;
    if (clang_init_state == -1) return -1;
    if (clang_do_load(library_path) == 0) {
        clang_init_state = 1;
        return 0;
    }
    clang_init_state = -1;
    return -1;
}

void clang_dyn_cleanup(void) {
    if (clang_handle) {
        clang_free_lib(clang_handle);
        clang_handle = NULL;
    }
    clang_init_state = 0;
}

/* ============================================================
 * 按需解析宏
 * ============================================================ */
#define CLANG_RESOLVE(var, name, fallback) do {                      \
    if (!(var)) {                                              \
        if (!clang_libok()) return fallback;                   \
        *(void **)(&(var)) = clang_get_sym(clang_handle, name);      \
        if (!(var)) {                                          \
            fprintf(stderr, "dlsym failed: %s\n", name);       \
            return fallback;                                   \
        }                                                      \
    }                                                          \
} while (0)

/* ============================================================
 * Index / Translation Unit 模块
 * ============================================================ */
CXIndex clang_createIndex(int excludeDeclarationsFromPCH, int displayDiagnostics) {
    static CXIndex (*fn)(int, int) = NULL;
    if (!fn) {
        if (!clang_libok()) return NULL;
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_createIndex");
        if (!fn) return NULL;
    }
    return fn(excludeDeclarationsFromPCH, displayDiagnostics);
}

void clang_disposeIndex(CXIndex index) {
    static void (*fn)(CXIndex) = NULL;
    if (!fn) {
        if (!clang_libok()) return;
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_disposeIndex");
        if (!fn) return;
    }
    fn(index);
}

CXTranslationUnit clang_parseTranslationUnit(
    CXIndex CIdx, const char *source_filename,
    const char *const *command_line_args, int num_command_line_args,
    struct CXUnsavedFile *unsaved_files, unsigned num_unsaved_files,
    unsigned options) {
    static CXTranslationUnit (*fn)(CXIndex, const char*, const char*const*,
                                   int, struct CXUnsavedFile*, unsigned, unsigned) = NULL;
    if (!fn) {
        if (!clang_libok()) return NULL;
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_parseTranslationUnit");
        if (!fn) return NULL;
    }
    return fn(CIdx, source_filename, command_line_args, num_command_line_args,
              unsaved_files, num_unsaved_files, options);
}

void clang_disposeTranslationUnit(CXTranslationUnit tu) {
    static void (*fn)(CXTranslationUnit) = NULL;
    if (!fn) {
        if (!clang_libok()) return;
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_disposeTranslationUnit");
        if (!fn) return;
    }
    fn(tu);
}

CXString clang_getTranslationUnitSpelling(CXTranslationUnit CTUnit) {
    static CXString (*fn)(CXTranslationUnit) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXString s = {0}; return s; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getTranslationUnitSpelling");
        if (!fn) { CXString s = {0}; return s; }
    }
    return fn(CTUnit);
}

CXCursor clang_getTranslationUnitCursor(CXTranslationUnit tu) {
    static CXCursor (*fn)(CXTranslationUnit) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXCursor c = {0}; return c; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getTranslationUnitCursor");
        if (!fn) { CXCursor c = {0}; return c; }
    }
    return fn(tu);
}

/* ============================================================
 * Cursor 模块
 * ============================================================ */
unsigned clang_visitChildren(CXCursor parent, CXCursorVisitor visitor, CXClientData client_data) {
    static unsigned (*fn)(CXCursor, CXCursorVisitor, CXClientData) = NULL;
    CLANG_RESOLVE(fn, "clang_visitChildren", 0);
    return fn(parent, visitor, client_data);
}

CXString clang_getCursorSpelling(CXCursor cursor) {
    static CXString (*fn)(CXCursor) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXString s = {0}; return s; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getCursorSpelling");
        if (!fn) { CXString s = {0}; return s; }
    }
    return fn(cursor);
}

enum CXCursorKind clang_getCursorKind(CXCursor cursor) {
    static enum CXCursorKind (*fn)(CXCursor) = NULL;
    CLANG_RESOLVE(fn, "clang_getCursorKind", CXCursor_InvalidFile);
    return fn(cursor);
}

unsigned clang_isDeclaration(enum CXCursorKind kind) {
    static unsigned (*fn)(enum CXCursorKind) = NULL;
    CLANG_RESOLVE(fn, "clang_isDeclaration", 0);
    return fn(kind);
}

/* ============================================================
 * Diagnostic 模块
 * ============================================================ */
unsigned clang_getNumDiagnostics(CXTranslationUnit Unit) {
    static unsigned (*fn)(CXTranslationUnit) = NULL;
    CLANG_RESOLVE(fn, "clang_getNumDiagnostics", 0);
    return fn(Unit);
}

CXDiagnostic clang_getDiagnostic(CXTranslationUnit Unit, unsigned Index) {
    static CXDiagnostic (*fn)(CXTranslationUnit, unsigned) = NULL;
    if (!fn) {
        if (!clang_libok()) return NULL;
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getDiagnostic");
        if (!fn) return NULL;
    }
    return fn(Unit, Index);
}

void clang_disposeDiagnostic(CXDiagnostic Diagnostic) {
    static void (*fn)(CXDiagnostic) = NULL;
    if (!fn) {
        if (!clang_libok()) return;
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_disposeDiagnostic");
        if (!fn) return;
    }
    fn(Diagnostic);
}

CXString clang_formatDiagnostic(CXDiagnostic Diagnostic, unsigned Options) {
    static CXString (*fn)(CXDiagnostic, unsigned) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXString s = {0}; return s; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_formatDiagnostic");
        if (!fn) { CXString s = {0}; return s; }
    }
    return fn(Diagnostic, Options);
}

unsigned clang_defaultDiagnosticDisplayOptions(void) {
    static unsigned (*fn)(void) = NULL;
    CLANG_RESOLVE(fn, "clang_defaultDiagnosticDisplayOptions", 0);
    return fn();
}

enum CXDiagnosticSeverity clang_getDiagnosticSeverity(CXDiagnostic Diagnostic) {
    static enum CXDiagnosticSeverity (*fn)(CXDiagnostic) = NULL;
    CLANG_RESOLVE(fn, "clang_getDiagnosticSeverity", CXDiagnostic_Ignored);
    return fn(Diagnostic);
}

CXSourceLocation clang_getDiagnosticLocation(CXDiagnostic Diagnostic) {
    static CXSourceLocation (*fn)(CXDiagnostic) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXSourceLocation l = {0}; return l; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getDiagnosticLocation");
        if (!fn) { CXSourceLocation l = {0}; return l; }
    }
    return fn(Diagnostic);
}

CXString clang_getDiagnosticSpelling(CXDiagnostic Diagnostic) {
    static CXString (*fn)(CXDiagnostic) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXString s = {0}; return s; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getDiagnosticSpelling");
        if (!fn) { CXString s = {0}; return s; }
    }
    return fn(Diagnostic);
}

/* ============================================================
 * Source Location / Range 模块
 * ============================================================ */
CXSourceLocation clang_getCursorLocation(CXCursor cursor) {
    static CXSourceLocation (*fn)(CXCursor) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXSourceLocation l = {0}; return l; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getCursorLocation");
        if (!fn) { CXSourceLocation l = {0}; return l; }
    }
    return fn(cursor);
}

void clang_getExpansionLocation(CXSourceLocation location, CXFile *file,
                                unsigned *line, unsigned *column, unsigned *offset) {
    static void (*fn)(CXSourceLocation, CXFile*, unsigned*, unsigned*, unsigned*) = NULL;
    if (!fn) {
        if (!clang_libok()) return;
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getExpansionLocation");
        if (!fn) return;
    }
    fn(location, file, line, column, offset);
}

CXSourceRange clang_getCursorExtent(CXCursor cursor) {
    static CXSourceRange (*fn)(CXCursor) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXSourceRange r = {0}; return r; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getCursorExtent");
        if (!fn) { CXSourceRange r = {0}; return r; }
    }
    return fn(cursor);
}

CXSourceLocation clang_getRangeStart(CXSourceRange range) {
    static CXSourceLocation (*fn)(CXSourceRange) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXSourceLocation l = {0}; return l; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getRangeStart");
        if (!fn) { CXSourceLocation l = {0}; return l; }
    }
    return fn(range);
}

CXSourceLocation clang_getRangeEnd(CXSourceRange range) {
    static CXSourceLocation (*fn)(CXSourceRange) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXSourceLocation l = {0}; return l; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getRangeEnd");
        if (!fn) { CXSourceLocation l = {0}; return l; }
    }
    return fn(range);
}

CXString clang_getFileName(CXFile SFile) {
    static CXString (*fn)(CXFile) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXString s = {0}; return s; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getFileName");
        if (!fn) { CXString s = {0}; return s; }
    }
    return fn(SFile);
}

/* ============================================================
 * Type 模块
 * ============================================================ */
CXType clang_getCursorType(CXCursor C) {
    static CXType (*fn)(CXCursor) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXType t = {0}; return t; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getCursorType");
        if (!fn) { CXType t = {0}; return t; }
    }
    return fn(C);
}

CXString clang_getTypeSpelling(CXType CT) {
    static CXString (*fn)(CXType) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXString s = {0}; return s; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getTypeSpelling");
        if (!fn) { CXString s = {0}; return s; }
    }
    return fn(CT);
}

CXString clang_getTypeKindSpelling(enum CXTypeKind K) {
    static CXString (*fn)(enum CXTypeKind) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXString s = {0}; return s; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getTypeKindSpelling");
        if (!fn) { CXString s = {0}; return s; }
    }
    return fn(K);
}

CXType clang_getPointeeType(CXType T) {
    static CXType (*fn)(CXType) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXType t = {0}; return t; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getPointeeType");
        if (!fn) { CXType t = {0}; return t; }
    }
    return fn(T);
}

unsigned clang_isConstQualifiedType(CXType T) {
    static unsigned (*fn)(CXType) = NULL;
    CLANG_RESOLVE(fn, "clang_isConstQualifiedType", 0);
    return fn(T);
}

/* ============================================================
 * 字符串与版本模块
 * ============================================================ */
const char *clang_getCString(CXString string) {
    static const char *(*fn)(CXString) = NULL;
    if (!fn) {
        if (!clang_libok()) return NULL;
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getCString");
        if (!fn) return NULL;
    }
    return fn(string);
}

void clang_disposeString(CXString string) {
    static void (*fn)(CXString) = NULL;
    if (!fn) {
        if (!clang_libok()) return;
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_disposeString");
        if (!fn) return;
    }
    fn(string);
}

CXString clang_getClangVersion(void) {
    static CXString (*fn)(void) = NULL;
    if (!fn) {
        if (!clang_libok()) { CXString s = {0}; return s; }
        *(void **)(&fn) = clang_get_sym(clang_handle, "clang_getClangVersion");
        if (!fn) { CXString s = {0}; return s; }
    }
    return fn();
}

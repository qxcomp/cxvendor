#!/usr/bin/env python3
"""生成 rtdb/re2_dyn.c。

RE2 没有 C API，全部公开 API 都是 C++ mangled 符号。本生成器直接用原始
mangled symbol 作为 C wrapper 的函数名；非静态成员函数第一个参数按 this 处理。
签名表由公开头文件人工固化，符号集由 nm -D 提供，并做缺失/内部符号审计。
"""
import os
import re
import subprocess
import sys
import pathlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import gen_tls_full as F

OUT = F.OUT
LIB = '/usr/lib/libre2.so'
DATA_SYM = '_ZN3re23RE27Options14kDefaultMaxMemE'
PROBE_SYM = '_ZNK3re23RE211ProgramSizeEv'

SV = 'std::basic_string_view<char, std::char_traits<char> >'
STR = 'std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >'
VINT = 'std::vector<int, std::allocator<int> >'
VSTR = ('std::vector<std::__cxx11::basic_string<char, std::char_traits<char>, '
        'std::allocator<char> >, std::allocator<std::__cxx11::basic_string<char, '
        'std::char_traits<char>, std::allocator<char> > > >')

# (公开头 demangled 名, 类别, 返回类型, C ABI 参数, 实参, fallback)
PUBLIC = [
    ('re2::RE2::RE2(char const*)', 'ctor', 'void *', 'void *self, const char *a0', 'self, a0', 'self'),
    ('re2::RE2::RE2(%s const&)' % STR, 'ctor', 'void *', 'void *self, void *a0', 'self, a0', 'self'),
    ('re2::RE2::RE2(%s)' % SV, 'ctor', 'void *', 'void *self, re2_sv a0', 'self, a0', 'self'),
    ('re2::RE2::RE2(%s, re2::RE2::Options const&)' % SV, 'ctor', 'void *', 'void *self, re2_sv a0, void *a1', 'self, a0, a1', 'self'),
    ('re2::RE2::~RE2()', 'dtor', 'void', 'void *self', 'self', None),
    ('re2::RE2::ProgramSize() const', 'normal', 'int', 'void *self', 'self', '0'),
    ('re2::RE2::ReverseProgramSize() const', 'normal', 'int', 'void *self', 'self', '0'),
    ('re2::RE2::ProgramFanout(%s*) const' % VINT, 'normal', 'int', 'void *self, void *a0', 'self, a0', '0'),
    ('re2::RE2::ReverseProgramFanout(%s*) const' % VINT, 'normal', 'int', 'void *self, void *a0', 'self, a0', '0'),
    ('re2::RE2::FullMatchN(%s, re2::RE2 const&, re2::RE2::Arg const* const*, int)' % SV,
     'normal', 'unsigned char', 're2_sv a0, void *a1, const void *const *a2, int a3', 'a0, a1, a2, a3', '0'),
    ('re2::RE2::PartialMatchN(%s, re2::RE2 const&, re2::RE2::Arg const* const*, int)' % SV,
     'normal', 'unsigned char', 're2_sv a0, void *a1, const void *const *a2, int a3', 'a0, a1, a2, a3', '0'),
    ('re2::RE2::ConsumeN(%s*, re2::RE2 const&, re2::RE2::Arg const* const*, int)' % SV,
     'normal', 'unsigned char', 're2_sv *a0, void *a1, const void *const *a2, int a3', 'a0, a1, a2, a3', '0'),
    ('re2::RE2::FindAndConsumeN(%s*, re2::RE2 const&, re2::RE2::Arg const* const*, int)' % SV,
     'normal', 'unsigned char', 're2_sv *a0, void *a1, const void *const *a2, int a3', 'a0, a1, a2, a3', '0'),
    ('re2::RE2::QuoteMeta[abi:cxx11](%s)' % SV, 'normal', 're2_std_string', 're2_sv a0', 'a0', 're2_string_zero'),
    ('re2::RE2::CheckRewriteString(%s, %s*) const' % (SV, STR),
     'normal', 'unsigned char', 'void *self, re2_sv a0, void *a1', 'self, a0, a1', '0'),
    ('re2::RE2::MaxSubmatch(%s)' % SV, 'normal', 'int', 're2_sv a0', 'a0', '0'),
    ('re2::RE2::NamedCapturingGroups[abi:cxx11]() const', 'normal', 'void *', 'void *self', 'self', 'NULL'),
    ('re2::RE2::CapturingGroupNames[abi:cxx11]() const', 'normal', 'void *', 'void *self', 'self', 'NULL'),
    ('re2::RE2::PossibleMatchRange(%s*, %s*, int) const' % (STR, STR),
     'normal', 'unsigned char', 'void *self, void *a0, void *a1, int a2', 'self, a0, a1, a2', '0'),
    ('re2::RE2::Match(%s, unsigned long, unsigned long, re2::RE2::Anchor, %s*, int) const' % (SV, SV),
     'normal', 'unsigned char', 'void *self, re2_sv a0, size_t a1, size_t a2, int a3, re2_sv *a4, int a5',
     'self, a0, a1, a2, a3, a4, a5', '0'),
    ('re2::RE2::Rewrite(%s*, %s, %s const*, int) const' % (STR, SV, SV),
     'normal', 'unsigned char', 'void *self, void *a0, re2_sv a1, const re2_sv *a2, int a3',
     'self, a0, a1, a2, a3', '0'),
    ('re2::RE2::Replace(%s*, re2::RE2 const&, %s)' % (STR, SV),
     'normal', 'unsigned char', 'void *a0, void *a1, re2_sv a2', 'a0, a1, a2', '0'),
    ('re2::RE2::GlobalReplace(%s*, re2::RE2 const&, %s)' % (STR, SV),
     'normal', 'int', 'void *a0, void *a1, re2_sv a2', 'a0, a1, a2', '0'),
    ('re2::RE2::Extract(%s, re2::RE2 const&, %s, %s*)' % (SV, SV, STR),
     'normal', 'unsigned char', 're2_sv a0, void *a1, re2_sv a2, void *a3', 'a0, a1, a2, a3', '0'),
    ('re2::RE2::FUZZING_ONLY_set_maximum_global_replace_count(int)',
     'normal', 'void', 'int a0', 'a0', None),
    ('re2::RE2::Options::Options(re2::RE2::CannedOptions)', 'ctor', 'void *', 'void *self, int a0', 'self, a0', 'self'),
    ('re2::RE2::Options::ParseFlags() const', 'normal', 'int', 'void *self', 'self', '0'),
    ('re2::RE2::Set::Set(re2::RE2::Options const&, re2::RE2::Anchor)', 'ctor', 'void *', 'void *self, void *a0, int a1', 'self, a0, a1', 'self'),
    ('re2::RE2::Set::~Set()', 'dtor', 'void', 'void *self', 'self', None),
    ('re2::RE2::Set::Set(re2::RE2::Set&&)', 'ctor', 'void *', 'void *self, void *a0', 'self, a0', 'self'),
    ('re2::RE2::Set::operator=(re2::RE2::Set&&)', 'normal', 'void *', 'void *self, void *a0', 'self, a0', 'self'),
    ('re2::RE2::Set::Add(%s, %s*)' % (SV, STR), 'normal', 'int', 'void *self, re2_sv a0, void *a1', 'self, a0, a1', '0'),
    ('re2::RE2::Set::Size() const', 'normal', 'int', 'void *self', 'self', '0'),
    ('re2::RE2::Set::Compile()', 'normal', 'unsigned char', 'void *self', 'self', '0'),
    ('re2::RE2::Set::Match(%s, %s*) const' % (SV, VINT), 'normal', 'unsigned char', 'void *self, re2_sv a0, void *a1', 'self, a0, a1', '0'),
    ('re2::RE2::Set::Match(%s, %s*, re2::RE2::Set::ErrorInfo*) const' % (SV, VINT),
     'normal', 'unsigned char', 'void *self, re2_sv a0, void *a1, void *a2', 'self, a0, a1, a2', '0'),
    ('re2::FilteredRE2::FilteredRE2()', 'ctor', 'void *', 'void *self', 'self', 'self'),
    ('re2::FilteredRE2::FilteredRE2(int)', 'ctor', 'void *', 'void *self, int a0', 'self, a0', 'self'),
    ('re2::FilteredRE2::~FilteredRE2()', 'dtor', 'void', 'void *self', 'self', None),
    ('re2::FilteredRE2::FilteredRE2(re2::FilteredRE2&&)', 'ctor', 'void *', 'void *self, void *a0', 'self, a0', 'self'),
    ('re2::FilteredRE2::operator=(re2::FilteredRE2&&)', 'normal', 'void *', 'void *self, void *a0', 'self, a0', 'self'),
    ('re2::FilteredRE2::Add(%s, re2::RE2::Options const&, int*)' % SV,
     'normal', 'int', 'void *self, re2_sv a0, void *a1, int *a2', 'self, a0, a1, a2', '0'),
    ('re2::FilteredRE2::Compile(%s*)' % VSTR, 'normal', 'void', 'void *self, void *a0', 'self, a0', None),
    ('re2::FilteredRE2::SlowFirstMatch(%s) const' % SV, 'normal', 'int', 'void *self, re2_sv a0', 'self, a0', '0'),
    ('re2::FilteredRE2::FirstMatch(%s, %s const&) const' % (SV, VINT), 'normal', 'int', 'void *self, re2_sv a0, void *a1', 'self, a0, a1', '0'),
    ('re2::FilteredRE2::AllMatches(%s, %s const&, %s*) const' % (SV, VINT, VINT),
     'normal', 'unsigned char', 'void *self, re2_sv a0, void *a1, void *a2', 'self, a0, a1, a2', '0'),
    ('re2::FilteredRE2::AllPotentials(%s const&, %s*) const' % (VINT, VINT),
     'normal', 'void', 'void *self, void *a0, void *a1', 'self, a0, a1', None),
]


def run(cmd, input_text=None, **kw):
    return subprocess.run(cmd, input=input_text, capture_output=True, text=True, **kw)


def norm(text):
    return re.sub(r'\s+', ' ', text).strip()


def exported_and_demangled():
    r = run(['nm', '-D', '--defined-only', LIB])
    if r.returncode:
        print('nm failed:', r.stderr[:1000], file=sys.stderr)
        sys.exit(1)
    syms = []
    for line in r.stdout.splitlines():
        p = line.split()
        if len(p) < 3 or p[1] not in ('T', 'D', 'B', 'R'):
            continue
        syms.append(p[2])
    dem = run(['c++filt'], input_text='\n'.join(syms) + '\n')
    if dem.returncode:
        print('c++filt failed:', dem.stderr[:1000], file=sys.stderr)
        sys.exit(1)
    mapping = {}
    for sym, text in zip(syms, dem.stdout.splitlines()):
        mapping[sym] = norm(text)
    return syms, mapping


LOADER = r'''
/* ============================================================
 * 平台适配层：统一动态库 API 与默认库名
 * ============================================================ */
#ifdef _WIN32
 #include <windows.h>
 typedef HMODULE lib_handle_t;
 static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
 static void *get_sym(lib_handle_t h, const char *name) {
     return (void *)GetProcAddress(h, name);
 }
 static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *lib_error(void) { return "LoadLibrary failed"; }
 #define PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *lib_handle_t;
 static lib_handle_t load_lib(const char *path) {
     return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
 }
 static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
 static const char *lib_error(void) { return dlerror(); }
 #define PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态与库加载
 * ============================================================ */
static lib_handle_t re2_handle = NULL;
static int re2_init_state = 0;

#define MAX_CANDIDATES 64
#define PATH_BUF_LEN   1024

static const char *re2_defaults[] = {
"libre2.so.11", "libre2.so", "libre2.dylib", "re2.dll"
};

static int re2_do_load(const char *library_path) {
    if (re2_handle) return 0;
    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0, i;

    if (library_path && *library_path && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    const char *explicit_lib = getenv("RE2_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", explicit_lib);
        candidates[n] = bufs[n]; n++;
    }
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
            while (p && *p && n < MAX_CANDIDATES - 2) {
                char *sep = strchr(p, PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    for (i = 0; i < (int)(sizeof(re2_defaults)/sizeof(re2_defaults[0])); i++) {
                        if (n >= MAX_CANDIDATES - 2) break;
                        snprintf(bufs[n], PATH_BUF_LEN, "%s/%s", p, re2_defaults[i]);
                        candidates[n] = bufs[n]; n++;
                    }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }
    for (i = 0; i < (int)(sizeof(re2_defaults)/sizeof(re2_defaults[0])) && n < MAX_CANDIDATES - 1; i++)
        candidates[n++] = re2_defaults[i];
    const char *home = getenv("HOME");
    if (home && *home && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s/.nix-profile/lib/%s", home, re2_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    if (n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s", re2_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        re2_handle = load_lib(candidates[i]);
        if (re2_handle) break;
    }
    if (!re2_handle) {
        fprintf(stderr, "re2_dyn: cannot load library (%s)\n", lib_error());
        return -1;
    }
    if (!get_sym(re2_handle, "PROBE_SYM")) {
        fprintf(stderr, "re2_dyn: loaded library does not export PROBE_SYM\n");
        free_lib(re2_handle); re2_handle = NULL;
        return -1;
    }
    fprintf(stderr, "re2_dyn: loaded %d %s\n", i, candidates[i]);
    re2_dyn_sync_globals();
    return 0;
}

void *re2_sym(const char *name) { return re2_handle ? get_sym(re2_handle, name) : NULL; }

int re2_libok(void) {
    if (re2_init_state == 0)
        re2_init_state = (re2_do_load(NULL) == 0) ? 1 : -1;
    return (re2_init_state == 1 && re2_handle != NULL) ? 1 : 0;
}

int re2_dyn_init(const char *library_path) {
    if (re2_do_load(library_path) == 0) { re2_init_state = 1; return 0; }
    if (re2_init_state == 0) re2_init_state = -1;
    return -1;
}

void re2_dyn_cleanup(void) {
    if (!re2_handle) re2_init_state = 0;
    else if (re2_init_state == -1) re2_init_state = 0;
}

int re2_dyn_has_symbol(const char *name) {
    if (!re2_libok()) return 0;
    return re2_sym(name) != NULL;
}
'''


WRAPMAC = r'''
/* ============================================================
 * WRAP 宏：首次调用 dlsym 并缓存；失败打印 stderr 并返回安全值
 * 注意：name 就是 C++ mangled symbol；非静态成员的第一个参数是 this。
 * ============================================================ */
#define WRAP_RE2_RET(ret, name, params, args, fallback)                    \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!re2_libok()) return fallback;                                \
        *(void **)(&fn) = re2_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "re2_dyn: dlsym failed: %s\n", #name);        \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define WRAP_RE2_VOID(name, params, args)                                 \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!re2_libok()) return;                                         \
        *(void **)(&fn) = re2_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "re2_dyn: dlsym failed: %s\n", #name);        \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}
'''


def main():
    pathlib.Path(OUT).mkdir(parents=True, exist_ok=True)
    syms, demangled = exported_and_demangled()
    by_name = {}
    for sym in syms:
        by_name.setdefault(demangled[sym], []).append(sym)

    wraps = []
    used = set()
    missing = []
    for key, kind, ret, params, args, fallback in PUBLIC:
        candidates = by_name.get(norm(key), [])
        if not candidates:
            missing.append(key)
            continue
        for sym in candidates:
            used.add(sym)
            if kind == 'dtor' or ret == 'void':
                wraps.append('WRAP_RE2_VOID(%s, (%s), (%s))' % (sym, params, args))
            else:
                wraps.append('WRAP_RE2_RET(%s, %s, (%s), (%s), %s)'
                             % (ret, sym, params, args, fallback))
    if missing:
        print('re2: 公开符号缺失 %d 个:' % len(missing), file=sys.stderr)
        for key in missing:
            print('  ' + key, file=sys.stderr)
        sys.exit(1)
    if DATA_SYM not in syms:
        print('re2: 数据符号缺失: ' + DATA_SYM, file=sys.stderr)
        sys.exit(1)
    if PROBE_SYM not in syms:
        print('re2: 探测符号缺失: ' + PROBE_SYM, file=sys.stderr)
        sys.exit(1)

    function_syms = {s for s in syms if demangled[s] not in ('',)}
    skipped = sorted(s for s in function_syms if s not in used and s != DATA_SYM)

    n_export = len(function_syms)
    header = '''/* ============================================================
 * re2_dyn.c — RE2 动态加载封装层（公开头文件 API 全量封装）
 *
 * 统计：导出函数 %d / 封装函数 %d / 跳过 %d / 数据符号 1
 * 设计要点：
 *   1. 单文件、无 .h；链接期不链接目标库（只 -ldl）。
 *   2. wrapper 函数名就是 C++ 原始 mangled symbol，不自定义 API。
 *   3. 非静态成员的第一个参数是 this；构造/析构的 C1/C2/D1/D2 全部封装。
 *   4. absl::string_view 按 libstdc++ 布局 {size_t len; const char *ptr;} 传递。
 *   5. 首次调用才 dlsym 并缓存；内部/私有导出符号列在文件尾 skip 清单。
 * ============================================================ */
''' % (n_export, len(wraps), len(skipped))

    s = [header]
    s.append('#define _POSIX_C_SOURCE 200809L\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <errno.h>\n')
    s.append('''/* ---------- C ABI 对齐类型 ---------- */
typedef struct {
    size_t len;
    const char *ptr;
} re2_sv;

typedef struct {
    void *ptr;
    size_t len;
    union {
        char bytes[16];
        void *align;
    } tail;
} re2_std_string;

static const re2_std_string re2_string_zero = {0};

/* ---------- 前向声明 ---------- */
void *re2_sym(const char *name);
void re2_dyn_sync_globals(void);

/* ---------- 数据符号副本 ---------- */
int %s = 0;

void re2_dyn_sync_globals(void) {
    void *p;
    if ((p = re2_sym("%s")) != NULL)
        memcpy(&%s, p, sizeof(%s));
}
''' % (DATA_SYM, DATA_SYM, DATA_SYM, DATA_SYM))
    s.append(LOADER.replace('PROBE_SYM', PROBE_SYM))
    s.append(WRAPMAC)
    s.append('/* ---------- 全量函数封装（原始 mangled symbol） ---------- */\n')
    s.append('\n'.join(wraps) + '\n')
    if skipped:
        s.append('/* ---------- 未封装的内部/私有导出符号，共 %d 个 ---------- */\n' % len(skipped))
        s.append('\n'.join('/* %s */' % x for x in skipped) + '\n')

    out = os.path.join(REPO_ROOT, 'rtdb', 're2_dyn.c')
    content = ''.join(s)
    open(out, 'w').write(content)
    print('re2 -> %s: %d 行, %d WRAP, 1 数据符号, skip %d'
          % (out, len(content.splitlines()), len(wraps), len(skipped)))


if __name__ == '__main__':
    main()

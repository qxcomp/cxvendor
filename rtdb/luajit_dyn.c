/* ============================================================
 * luajit_dyn.c - dlopen 模拟链接：LuaJIT 2.1 C API（Lua 5.1 兼容）
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照 lua.h / lauxlib.h / lualib.h 的 144 个公开
 * 函数声明；按 ABI 等价内嵌 lua_Integer/lua_Number/lua_State 等
 * 类型，不引入目标库头文件。函数名保持原生原样。
 * 说明：lua_setlevel 有公开声明但当前 LuaJIT 2.1 共享库未导出，
 *       测试按 143/144 计数并显式打印该项。
 * 链接期不需要 libluajit，仅需 -ldl。
 * 加载顺序：显式路径 > LUAJIT_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > 平台默认；成功后不卸载。
 * 加载日志照搬 opendal_dyn.c 当前 diff。
 * ============================================================ */

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* ---------- 平台适配层 ---------- */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE lib_handle_t;
  static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
  static void *get_sym(lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
  static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *lib_error(void) { return "LoadLibrary failed"; }
  #define PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *lib_handle_t;
  static lib_handle_t load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
  static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
  static const char *lib_error(void) { return dlerror(); }
  #define PATH_SEP ':'
#endif

/* ---------- 类型（头文件原样内嵌，字段顺序即 ABI） ---------- */
typedef struct lua_State lua_State;
typedef struct lua_Debug lua_Debug;
typedef struct luaL_Reg luaL_Reg;
typedef struct luaL_Buffer luaL_Buffer;
typedef double lua_Number;
typedef ptrdiff_t lua_Integer;
typedef int (*lua_CFunction)(lua_State *L);
typedef const char *(*lua_Reader)(lua_State *L, void *ud, size_t *sz);
typedef int (*lua_Writer)(lua_State *L, const void *p, size_t sz, void *ud);
typedef void *(*lua_Alloc)(void *ud, void *ptr, size_t osize, size_t nsize);
typedef void (*lua_Hook)(lua_State *L, lua_Debug *ar);

#define LUA_IDSIZE 60
struct lua_Debug {
    int event;
    const char *name;
    const char *namewhat;
    const char *what;
    const char *source;
    int currentline;
    int nups;
    int linedefined;
    int lastlinedefined;
    char short_src[LUA_IDSIZE];
    int i_ci;
};
struct luaL_Reg {
    const char *name;
    lua_CFunction func;
};
#define LUAL_BUFFERSIZE (BUFSIZ > 16384 ? 8192 : BUFSIZ)
struct luaL_Buffer {
    char *p;
    int lvl;
    lua_State *L;
    char buffer[LUAL_BUFFERSIZE];
};

/* ---------- 动态层状态 ---------- */
static lib_handle_t luajit_handle = NULL;
static int luajit_init_state = 0;

static void *luajit_sym(const char *name) {
    return luajit_handle ? get_sym(luajit_handle, name) : NULL;
}

static int luajit_do_load(const char *library_path) {
    static char bufs[64][1024];
    const char *candidates[64];
    const char *so_soname = "libluajit-5.1.so.2";
    const char *so_ver = "libluajit-5.1.so.2";
    const char *so_plain = "libluajit-5.1.so";
    int n = 0, i;
    int res = -1;

    if (luajit_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("LUAJIT_DYN_LIB");
        if (e && *e) {
            snprintf(bufs[n], sizeof bufs[0], "%s", e);
            candidates[n] = bufs[n]; n++;
        }
    }
    {
#ifdef _WIN32
        const char *env_path = getenv("PATH");
#elif defined(__APPLE__)
        const char *env_path = getenv("DYLD_LIBRARY_PATH");
#else
        const char *env_path = getenv("LD_LIBRARY_PATH");
#endif
        if (env_path && *env_path) {
            char *copy = strdup(env_path);
            if (copy) {
                char *p = copy;
                while (p && n < 60) {
                    char *sep = strchr(p, PATH_SEP);
                    if (sep) *sep = '\0';
                    if (*p) {
                        snprintf(bufs[n], sizeof bufs[0], "%s%c%s", p, PATH_SEP, so_soname);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s%c%s", p, PATH_SEP, so_plain);
                        candidates[n] = bufs[n]; n++;
                    }
                    p = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    {
        snprintf(bufs[n], sizeof bufs[0], "%s", so_soname);
        candidates[n] = bufs[n]; n++;
        snprintf(bufs[n], sizeof bufs[0], "%s", so_ver);
        candidates[n] = bufs[n]; n++;
        snprintf(bufs[n], sizeof bufs[0], "%s", so_plain);
        candidates[n] = bufs[n]; n++;
    }
    {
#ifdef __linux__
        static const char *dirs[] = { "/usr/lib", "/usr/lib64", "/usr/local/lib", "/lib" };
        unsigned int d;
        for (d = 0; d < sizeof dirs / sizeof dirs[0] && n < 60; d++) {
            snprintf(bufs[n], sizeof bufs[0], "%s/%s", dirs[d], so_soname);
            candidates[n] = bufs[n]; n++;
        }
#endif
    }

    candidates[n] = NULL;
    for (i = 0; i < n; i++) {
        lib_handle_t h = load_lib(candidates[i]);
        if (h) { luajit_handle = h; res = i; break; }
    }
    if (res < 0) {
        fprintf(stderr, "luajit_dyn: cannot load luajit (%s)\n", lib_error());
        return -1;
    }
    fprintf(stderr, "luajit_dyn: loaded luajit %d %s\n", res, candidates[res]);
    return 0;
}

int luajit_dyn_init(const char *library_path) {
    if (luajit_do_load(library_path) == 0) {
        luajit_init_state = 1;
        return 0;
    }
    if (luajit_init_state == 0) luajit_init_state = -1;
    return -1;
}

void luajit_dyn_cleanup(void) {
    if (luajit_handle) { free_lib(luajit_handle); luajit_handle = NULL; }
    luajit_init_state = 0;
}

int luajit_dyn_libok(void) { return luajit_init_state == 1 && luajit_handle != NULL; }

int luajit_dyn_has_symbol(const char *name) {
    if (!luajit_dyn_libok()) return 0;
    return luajit_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!luajit_dyn_libok()) return fallback;                              \
        *(void **)(&fn) = luajit_sym(#name);                                   \
        if (!fn) {                                                             \
            fprintf(stderr, "luajit_dyn: dlsym failed: %s\n", #name);          \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!luajit_dyn_libok()) return;                                       \
        *(void **)(&fn) = luajit_sym(#name);                                   \
        if (!fn) {                                                             \
            fprintf(stderr, "luajit_dyn: dlsym failed: %s\n", #name);          \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ---------- 144 个公开函数（函数名原样） ---------- */
WRAP_RET(lua_State *, lua_newstate, (lua_Alloc f, void *ud), (f, ud), NULL)
WRAP_VOID(lua_close, (lua_State *L), (L))
WRAP_RET(lua_State *, lua_newthread, (lua_State *L), (L), NULL)
WRAP_RET(lua_CFunction, lua_atpanic, (lua_State *L, lua_CFunction panicf), (L, panicf), NULL)
WRAP_RET(int, lua_gettop, (lua_State *L), (L), 0)
WRAP_VOID(lua_settop, (lua_State *L, int idx), (L, idx))
WRAP_VOID(lua_pushvalue, (lua_State *L, int idx), (L, idx))
WRAP_VOID(lua_remove, (lua_State *L, int idx), (L, idx))
WRAP_VOID(lua_insert, (lua_State *L, int idx), (L, idx))
WRAP_VOID(lua_replace, (lua_State *L, int idx), (L, idx))
WRAP_RET(int, lua_checkstack, (lua_State *L, int sz), (L, sz), 0)
WRAP_VOID(lua_xmove, (lua_State *from, lua_State *to, int n), (from, to, n))
WRAP_RET(int, lua_isnumber, (lua_State *L, int idx), (L, idx), 0)
WRAP_RET(int, lua_isstring, (lua_State *L, int idx), (L, idx), 0)
WRAP_RET(int, lua_iscfunction, (lua_State *L, int idx), (L, idx), 0)
WRAP_RET(int, lua_isuserdata, (lua_State *L, int idx), (L, idx), 0)
WRAP_RET(int, lua_type, (lua_State *L, int idx), (L, idx), 0)
WRAP_RET(const char *, lua_typename, (lua_State *L, int tp), (L, tp), NULL)
WRAP_RET(int, lua_equal, (lua_State *L, int idx1, int idx2), (L, idx1, idx2), 0)
WRAP_RET(int, lua_rawequal, (lua_State *L, int idx1, int idx2), (L, idx1, idx2), 0)
WRAP_RET(int, lua_lessthan, (lua_State *L, int idx1, int idx2), (L, idx1, idx2), 0)
WRAP_RET(lua_Number, lua_tonumber, (lua_State *L, int idx), (L, idx), 0)
WRAP_RET(lua_Integer, lua_tointeger, (lua_State *L, int idx), (L, idx), 0)
WRAP_RET(int, lua_toboolean, (lua_State *L, int idx), (L, idx), 0)
WRAP_RET(const char *, lua_tolstring, (lua_State *L, int idx, size_t *len), (L, idx, len), NULL)
WRAP_RET(size_t, lua_objlen, (lua_State *L, int idx), (L, idx), 0)
WRAP_RET(lua_CFunction, lua_tocfunction, (lua_State *L, int idx), (L, idx), NULL)
WRAP_RET(void *, lua_touserdata, (lua_State *L, int idx), (L, idx), NULL)
WRAP_RET(lua_State *, lua_tothread, (lua_State *L, int idx), (L, idx), NULL)
WRAP_RET(const void *, lua_topointer, (lua_State *L, int idx), (L, idx), NULL)
WRAP_VOID(lua_pushnil, (lua_State *L), (L))
WRAP_VOID(lua_pushnumber, (lua_State *L, lua_Number n), (L, n))
WRAP_VOID(lua_pushinteger, (lua_State *L, lua_Integer n), (L, n))
WRAP_VOID(lua_pushlstring, (lua_State *L, const char *s, size_t l), (L, s, l))
WRAP_VOID(lua_pushstring, (lua_State *L, const char *s), (L, s))
WRAP_RET(const char *, lua_pushvfstring, (lua_State *L, const char *fmt, va_list argp), (L, fmt, argp), NULL)
/* lua_pushfstring 是变参函数：用 __builtin_apply 原样转发寄存器参数。 */
const char *lua_pushfstring(lua_State *L, const char *fmt, ...) {
    static const char *(*fn)(lua_State *, const char *, ...) = NULL;
    void *r;
    (void)L; (void)fmt; /* 参数由 __builtin_apply_args() 整体转发 */
    if (!fn) {
        if (!luajit_dyn_libok()) return NULL;
        *(void **)(&fn) = luajit_sym("lua_pushfstring");
        if (!fn) {
            fprintf(stderr, "luajit_dyn: dlsym failed: lua_pushfstring\n");
            return NULL;
        }
    }
    r = __builtin_apply((void (*)(void))fn, __builtin_apply_args(), 256);
    __builtin_return(r);
}
WRAP_VOID(lua_pushcclosure, (lua_State *L, lua_CFunction f, int n), (L, f, n))
WRAP_VOID(lua_pushboolean, (lua_State *L, int b), (L, b))
WRAP_VOID(lua_pushlightuserdata, (lua_State *L, void *p), (L, p))
WRAP_RET(int, lua_pushthread, (lua_State *L), (L), 0)
WRAP_VOID(lua_gettable, (lua_State *L, int idx), (L, idx))
WRAP_VOID(lua_getfield, (lua_State *L, int idx, const char *k), (L, idx, k))
WRAP_VOID(lua_rawget, (lua_State *L, int idx), (L, idx))
WRAP_VOID(lua_rawgeti, (lua_State *L, int idx, int n), (L, idx, n))
WRAP_VOID(lua_createtable, (lua_State *L, int narr, int nrec), (L, narr, nrec))
WRAP_RET(void *, lua_newuserdata, (lua_State *L, size_t sz), (L, sz), NULL)
WRAP_RET(int, lua_getmetatable, (lua_State *L, int objindex), (L, objindex), 0)
WRAP_VOID(lua_getfenv, (lua_State *L, int idx), (L, idx))
WRAP_VOID(lua_settable, (lua_State *L, int idx), (L, idx))
WRAP_VOID(lua_setfield, (lua_State *L, int idx, const char *k), (L, idx, k))
WRAP_VOID(lua_rawset, (lua_State *L, int idx), (L, idx))
WRAP_VOID(lua_rawseti, (lua_State *L, int idx, int n), (L, idx, n))
WRAP_RET(int, lua_setmetatable, (lua_State *L, int objindex), (L, objindex), 0)
WRAP_RET(int, lua_setfenv, (lua_State *L, int idx), (L, idx), 0)
WRAP_VOID(lua_call, (lua_State *L, int nargs, int nresults), (L, nargs, nresults))
WRAP_RET(int, lua_pcall, (lua_State *L, int nargs, int nresults, int errfunc), (L, nargs, nresults, errfunc), 0)
WRAP_RET(int, lua_cpcall, (lua_State *L, lua_CFunction func, void *ud), (L, func, ud), 0)
WRAP_RET(int, lua_load, (lua_State *L, lua_Reader reader, void *dt, const char *chunkname), (L, reader, dt, chunkname), 0)
WRAP_RET(int, lua_dump, (lua_State *L, lua_Writer writer, void *data), (L, writer, data), 0)
WRAP_RET(int, lua_yield, (lua_State *L, int nresults), (L, nresults), 0)
WRAP_RET(int, lua_resume, (lua_State *L, int narg), (L, narg), 0)
WRAP_RET(int, lua_status, (lua_State *L), (L), 0)
WRAP_RET(int, lua_gc, (lua_State *L, int what, int data), (L, what, data), 0)
WRAP_RET(int, lua_error, (lua_State *L), (L), 0)
WRAP_RET(int, lua_next, (lua_State *L, int idx), (L, idx), 0)
WRAP_VOID(lua_concat, (lua_State *L, int n), (L, n))
WRAP_RET(lua_Alloc, lua_getallocf, (lua_State *L, void **ud), (L, ud), NULL)
WRAP_VOID(lua_setallocf, (lua_State *L, lua_Alloc f, void *ud), (L, f, ud))
WRAP_VOID(lua_setlevel, (lua_State *from, lua_State *to), (from, to))
WRAP_RET(int, lua_getstack, (lua_State *L, int level, lua_Debug *ar), (L, level, ar), 0)
WRAP_RET(int, lua_getinfo, (lua_State *L, const char *what, lua_Debug *ar), (L, what, ar), 0)
WRAP_RET(const char *, lua_getlocal, (lua_State *L, const lua_Debug *ar, int n), (L, ar, n), NULL)
WRAP_RET(const char *, lua_setlocal, (lua_State *L, const lua_Debug *ar, int n), (L, ar, n), NULL)
WRAP_RET(const char *, lua_getupvalue, (lua_State *L, int funcindex, int n), (L, funcindex, n), NULL)
WRAP_RET(const char *, lua_setupvalue, (lua_State *L, int funcindex, int n), (L, funcindex, n), NULL)
WRAP_RET(int, lua_sethook, (lua_State *L, lua_Hook func, int mask, int count), (L, func, mask, count), 0)
WRAP_RET(lua_Hook, lua_gethook, (lua_State *L), (L), NULL)
WRAP_RET(int, lua_gethookmask, (lua_State *L), (L), 0)
WRAP_RET(int, lua_gethookcount, (lua_State *L), (L), 0)
WRAP_RET(void *, lua_upvalueid, (lua_State *L, int idx, int n), (L, idx, n), NULL)
WRAP_VOID(lua_upvaluejoin, (lua_State *L, int idx1, int n1, int idx2, int n2), (L, idx1, n1, idx2, n2))
WRAP_RET(int, lua_loadx, (lua_State *L, lua_Reader reader, void *dt, const char *chunkname, const char *mode), (L, reader, dt, chunkname, mode), 0)
WRAP_RET(const lua_Number *, lua_version, (lua_State *L), (L), NULL)
WRAP_VOID(lua_copy, (lua_State *L, int fromidx, int toidx), (L, fromidx, toidx))
WRAP_RET(lua_Number, lua_tonumberx, (lua_State *L, int idx, int *isnum), (L, idx, isnum), 0)
WRAP_RET(lua_Integer, lua_tointegerx, (lua_State *L, int idx, int *isnum), (L, idx, isnum), 0)
WRAP_RET(int, lua_isyieldable, (lua_State *L), (L), 0)
WRAP_VOID(luaL_openlib, (lua_State *L, const char *libname, const luaL_Reg *l, int nup), (L, libname, l, nup))
WRAP_VOID(luaL_register, (lua_State *L, const char *libname, const luaL_Reg *l), (L, libname, l))
WRAP_RET(int, luaL_getmetafield, (lua_State *L, int obj, const char *e), (L, obj, e), 0)
WRAP_RET(int, luaL_callmeta, (lua_State *L, int obj, const char *e), (L, obj, e), 0)
WRAP_RET(int, luaL_typerror, (lua_State *L, int narg, const char *tname), (L, narg, tname), 0)
WRAP_RET(int, luaL_argerror, (lua_State *L, int numarg, const char *extramsg), (L, numarg, extramsg), 0)
WRAP_RET(const char *, luaL_checklstring, (lua_State *L, int numArg, size_t *l), (L, numArg, l), NULL)
WRAP_RET(const char *, luaL_optlstring, (lua_State *L, int numArg, const char *def, size_t *l), (L, numArg, def, l), NULL)
WRAP_RET(lua_Number, luaL_checknumber, (lua_State *L, int numArg), (L, numArg), 0)
WRAP_RET(lua_Number, luaL_optnumber, (lua_State *L, int nArg, lua_Number def), (L, nArg, def), 0)
WRAP_RET(lua_Integer, luaL_checkinteger, (lua_State *L, int numArg), (L, numArg), 0)
WRAP_RET(lua_Integer, luaL_optinteger, (lua_State *L, int nArg, lua_Integer def), (L, nArg, def), 0)
WRAP_VOID(luaL_checkstack, (lua_State *L, int sz, const char *msg), (L, sz, msg))
WRAP_VOID(luaL_checktype, (lua_State *L, int narg, int t), (L, narg, t))
WRAP_VOID(luaL_checkany, (lua_State *L, int narg), (L, narg))
WRAP_RET(int, luaL_newmetatable, (lua_State *L, const char *tname), (L, tname), 0)
WRAP_RET(void *, luaL_checkudata, (lua_State *L, int ud, const char *tname), (L, ud, tname), NULL)
WRAP_VOID(luaL_where, (lua_State *L, int lvl), (L, lvl))
/* luaL_error 是变参函数：用 __builtin_apply 原样转发寄存器参数。 */
int luaL_error(lua_State *L, const char *fmt, ...) {
    static int (*fn)(lua_State *, const char *, ...) = NULL;
    void *r;
    (void)L; (void)fmt; /* 参数由 __builtin_apply_args() 整体转发 */
    if (!fn) {
        if (!luajit_dyn_libok()) return 0;
        *(void **)(&fn) = luajit_sym("luaL_error");
        if (!fn) {
            fprintf(stderr, "luajit_dyn: dlsym failed: luaL_error\n");
            return 0;
        }
    }
    r = __builtin_apply((void (*)(void))fn, __builtin_apply_args(), 256);
    __builtin_return(r);
}
WRAP_RET(int, luaL_checkoption, (lua_State *L, int narg, const char *def, const char *const lst[]), (L, narg, def, lst), 0)
WRAP_RET(int, luaL_ref, (lua_State *L, int t), (L, t), 0)
WRAP_VOID(luaL_unref, (lua_State *L, int t, int ref), (L, t, ref))
WRAP_RET(int, luaL_loadfile, (lua_State *L, const char *filename), (L, filename), 0)
WRAP_RET(int, luaL_loadbuffer, (lua_State *L, const char *buff, size_t sz, const char *name), (L, buff, sz, name), 0)
WRAP_RET(int, luaL_loadstring, (lua_State *L, const char *s), (L, s), 0)
WRAP_RET(lua_State *, luaL_newstate, (void), (), NULL)
WRAP_RET(const char *, luaL_gsub, (lua_State *L, const char *s, const char *p, const char *r), (L, s, p, r), NULL)
WRAP_RET(const char *, luaL_findtable, (lua_State *L, int idx, const char *fname, int szhint), (L, idx, fname, szhint), NULL)
WRAP_RET(int, luaL_fileresult, (lua_State *L, int stat, const char *fname), (L, stat, fname), 0)
WRAP_RET(int, luaL_execresult, (lua_State *L, int stat), (L, stat), 0)
WRAP_RET(int, luaL_loadfilex, (lua_State *L, const char *filename, const char *mode), (L, filename, mode), 0)
WRAP_RET(int, luaL_loadbufferx, (lua_State *L, const char *buff, size_t sz, const char *name, const char *mode), (L, buff, sz, name, mode), 0)
WRAP_VOID(luaL_traceback, (lua_State *L, lua_State *L1, const char *msg, int level), (L, L1, msg, level))
WRAP_VOID(luaL_setfuncs, (lua_State *L, const luaL_Reg *l, int nup), (L, l, nup))
WRAP_VOID(luaL_pushmodule, (lua_State *L, const char *modname, int sizehint), (L, modname, sizehint))
WRAP_RET(void *, luaL_testudata, (lua_State *L, int ud, const char *tname), (L, ud, tname), NULL)
WRAP_VOID(luaL_setmetatable, (lua_State *L, const char *tname), (L, tname))
WRAP_VOID(luaL_buffinit, (lua_State *L, luaL_Buffer *B), (L, B))
WRAP_RET(char *, luaL_prepbuffer, (luaL_Buffer *B), (B), NULL)
WRAP_VOID(luaL_addlstring, (luaL_Buffer *B, const char *s, size_t l), (B, s, l))
WRAP_VOID(luaL_addstring, (luaL_Buffer *B, const char *s), (B, s))
WRAP_VOID(luaL_addvalue, (luaL_Buffer *B), (B))
WRAP_VOID(luaL_pushresult, (luaL_Buffer *B), (B))
WRAP_RET(int, luaopen_base, (lua_State *L), (L), 0)
WRAP_RET(int, luaopen_math, (lua_State *L), (L), 0)
WRAP_RET(int, luaopen_string, (lua_State *L), (L), 0)
WRAP_RET(int, luaopen_table, (lua_State *L), (L), 0)
WRAP_RET(int, luaopen_io, (lua_State *L), (L), 0)
WRAP_RET(int, luaopen_os, (lua_State *L), (L), 0)
WRAP_RET(int, luaopen_package, (lua_State *L), (L), 0)
WRAP_RET(int, luaopen_debug, (lua_State *L), (L), 0)
WRAP_RET(int, luaopen_bit, (lua_State *L), (L), 0)
WRAP_RET(int, luaopen_jit, (lua_State *L), (L), 0)
WRAP_RET(int, luaopen_ffi, (lua_State *L), (L), 0)
WRAP_RET(int, luaopen_string_buffer, (lua_State *L), (L), 0)
WRAP_VOID(luaL_openlibs, (lua_State *L), (L))

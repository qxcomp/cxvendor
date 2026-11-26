/* ============================================================
 * lmdb_dyn.c - dlopen 模拟链接：LMDB (lmdb.h)
 * ------------------------------------------------------------
 * 覆盖 lmdb.h 公开 API 全部 56 个导出函数。
 * 13 个 mdb_midl_* / mdb_mid2l_* 属于内部 midl.h（不安装），不封装。
 * 无数据符号（nm -D 只有 T）。
 *
 * 单文件、无 .h、无 wrapper；链接期不需要 liblmdb。
 * 加载顺序：显式路径 > LMDB_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 * 注意：liblmdb.so 没有 SONAME，候选表同时含 liblmdb.so 与 .so.0。
 *
 * 用法：
 *   lmdb_dyn_init(NULL);
 *   ... 原版 mdb_* 直接调用 ...
 *   lmdb_dyn_cleanup();
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

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

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<lmdb.h>)
#    include <lmdb.h>
#    define LMDB_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef LMDB_DYN_HAVE_HEADER
/* ---------- 类型（lmdb.h） ---------- */
typedef struct MDB_env MDB_env;
typedef struct MDB_txn MDB_txn;
typedef struct MDB_cursor MDB_cursor;
typedef unsigned int MDB_dbi;

#ifdef _WIN32
typedef void *mdb_filehandle_t;
#else
typedef int mdb_filehandle_t;
#endif
typedef mode_t mdb_mode_t;

typedef struct MDB_val {
    size_t mv_size;
    void  *mv_data;
} MDB_val;

typedef struct MDB_stat {
    unsigned int ms_psize;
    unsigned int ms_depth;
    size_t       ms_branch_pages;
    size_t       ms_leaf_pages;
    size_t       ms_overflow_pages;
    size_t       ms_entries;
} MDB_stat;

typedef struct MDB_envinfo {
    void        *me_mapaddr;
    size_t       me_mapsize;
    size_t       me_last_pgno;
    size_t       me_last_txnid;
    unsigned int me_maxreaders;
    unsigned int me_numreaders;
} MDB_envinfo;

typedef int  MDB_cmp_func(const MDB_val *a, const MDB_val *b);
typedef void MDB_rel_func(MDB_val *item, void *oldptr, void *newptr, void *relctx);
typedef void MDB_assert_func(MDB_env *env, const char *msg);
typedef int  MDB_msg_func(const char *msg, void *ctx);

typedef int MDB_cursor_op; /* enum ABI == int */

#endif /* LMDB_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t lmdb_handle = NULL;
static int lmdb_init_state = 0;

static void *lmdb_sym(const char *name) {
    return lmdb_handle ? get_sym(lmdb_handle, name) : NULL;
}

static int lmdb_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;

    if (lmdb_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("LMDB_DYN_LIB");
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
                char *q = copy;
                while (q && *q && n < 80) {
                    char *sep = strchr(q, PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/liblmdb.so", q);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/liblmdb.so.0", q);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = "liblmdb.so";
    candidates[n++] = "liblmdb.so.0";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/liblmdb.so", home);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/liblmdb.so.0", home);
            candidates[n] = bufs[n]; n++;
        }
        candidates[n++] = "/nix/var/nix/profiles/default/lib/liblmdb.so";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/liblmdb.so.0";
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/liblmdb.so";
    candidates[n++] = "/usr/local/lib/liblmdb.so";
    candidates[n++] = "/opt/homebrew/lib/liblmdb.so.0";
    candidates[n++] = "/usr/local/lib/liblmdb.so.0";
#elif defined(_WIN32)
    candidates[n++] = "liblmdb.dll";
#else
    candidates[n++] = "/usr/local/lib/liblmdb.so";
    candidates[n++] = "/usr/lib/liblmdb.so";
    candidates[n++] = "/usr/local/lib/liblmdb.so.0";
    candidates[n++] = "/usr/lib/liblmdb.so.0";
#endif

    for (i = 0; i < n; i++) {
        lmdb_handle = load_lib(candidates[i]);
        if (lmdb_handle) break;
    }
    if (!lmdb_handle || !lmdb_sym("mdb_env_create")) {
        fprintf(stderr, "lmdb_dyn: cannot load lmdb (%s)\n", lib_error());
        if (lmdb_handle) { free_lib(lmdb_handle); lmdb_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "lmdb_dyn: loaded lmdb\n");
    return 0;
}

int lmdb_dyn_libok(void) {
    if (lmdb_init_state == 0)
        lmdb_init_state = (lmdb_do_load(NULL) == 0) ? 1 : -1;
    return (lmdb_init_state == 1 && lmdb_handle != NULL) ? 1 : 0;
}

int lmdb_dyn_init(const char *library_path) {
    if (lmdb_do_load(library_path) == 0) {
        lmdb_init_state = 1;
        return 0;
    }
    if (lmdb_init_state == 0) lmdb_init_state = -1;
    return -1;
}

void lmdb_dyn_cleanup(void) {
    /* 仅复位加载状态，不卸载已成功加载的库（dlclose 不安全） */
    if (!lmdb_handle) lmdb_init_state = 0;
    else if (lmdb_init_state == -1) lmdb_init_state = 0;
}

int lmdb_dyn_has_symbol(const char *name) {
    if (!lmdb_dyn_libok()) return 0;
    return lmdb_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                          \
    static ret (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!lmdb_dyn_libok()) return fallback;                            \
        *(void **)(&fn) = lmdb_sym(#name);                                 \
        if (!fn) {                                                         \
            fprintf(stderr, "lmdb_dyn: dlsym failed: %s\n", #name);        \
            return fallback;                                               \
        }                                                                  \
    }                                                                      \
    return fn args;                                                        \
}

#define WRAP_VOID(name, params, args)                                      \
void name params {                                                         \
    static void (*fn) params = NULL;                                       \
    if (!fn) {                                                             \
        if (!lmdb_dyn_libok()) return;                                     \
        *(void **)(&fn) = lmdb_sym(#name);                                 \
        if (!fn) {                                                         \
            fprintf(stderr, "lmdb_dyn: dlsym failed: %s\n", #name);        \
            return;                                                        \
        }                                                                  \
    }                                                                      \
    fn args;                                                               \
}

/* ============================================================
 * 导出函数封装（lmdb.h 公开 API 56 个）
 * ============================================================ */
WRAP_RET(char *, mdb_version, (int *a0, int *a1, int *a2), (a0, a1, a2), NULL)
WRAP_RET(char *, mdb_strerror, (int a0), (a0), NULL)
WRAP_RET(int, mdb_env_create, (MDB_env **a0), (a0), -1)
WRAP_RET(int, mdb_env_open, (MDB_env *a0, const char *a1, unsigned int a2, mdb_mode_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, mdb_env_copy, (MDB_env *a0, const char *a1), (a0, a1), -1)
WRAP_RET(int, mdb_env_copyfd, (MDB_env *a0, mdb_filehandle_t a1), (a0, a1), -1)
WRAP_RET(int, mdb_env_copy2, (MDB_env *a0, const char *a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, mdb_env_copyfd2, (MDB_env *a0, mdb_filehandle_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, mdb_env_stat, (MDB_env *a0, MDB_stat *a1), (a0, a1), -1)
WRAP_RET(int, mdb_env_info, (MDB_env *a0, MDB_envinfo *a1), (a0, a1), -1)
WRAP_RET(int, mdb_env_sync, (MDB_env *a0, int a1), (a0, a1), -1)
WRAP_VOID(mdb_env_close, (MDB_env *a0), (a0))
WRAP_RET(int, mdb_env_set_flags, (MDB_env *a0, unsigned int a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, mdb_env_get_flags, (MDB_env *a0, unsigned int *a1), (a0, a1), -1)
WRAP_RET(int, mdb_env_get_path, (MDB_env *a0, const char **a1), (a0, a1), -1)
WRAP_RET(int, mdb_env_get_fd, (MDB_env *a0, mdb_filehandle_t *a1), (a0, a1), -1)
WRAP_RET(int, mdb_env_set_mapsize, (MDB_env *a0, size_t a1), (a0, a1), -1)
WRAP_RET(int, mdb_env_set_maxreaders, (MDB_env *a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, mdb_env_get_maxreaders, (MDB_env *a0, unsigned int *a1), (a0, a1), -1)
WRAP_RET(int, mdb_env_set_maxdbs, (MDB_env *a0, MDB_dbi a1), (a0, a1), -1)
WRAP_RET(int, mdb_env_get_maxkeysize, (MDB_env *a0), (a0), -1)
WRAP_RET(int, mdb_env_set_userctx, (MDB_env *a0, void *a1), (a0, a1), -1)
WRAP_RET(void *, mdb_env_get_userctx, (MDB_env *a0), (a0), NULL)
WRAP_RET(int, mdb_env_set_assert, (MDB_env *a0, MDB_assert_func *a1), (a0, a1), -1)
WRAP_RET(int, mdb_txn_begin, (MDB_env *a0, MDB_txn *a1, unsigned int a2, MDB_txn **a3), (a0, a1, a2, a3), -1)
WRAP_RET(MDB_env *, mdb_txn_env, (MDB_txn *a0), (a0), NULL)
WRAP_RET(size_t, mdb_txn_id, (MDB_txn *a0), (a0), 0)
WRAP_RET(int, mdb_txn_commit, (MDB_txn *a0), (a0), -1)
WRAP_VOID(mdb_txn_abort, (MDB_txn *a0), (a0))
WRAP_VOID(mdb_txn_reset, (MDB_txn *a0), (a0))
WRAP_RET(int, mdb_txn_renew, (MDB_txn *a0), (a0), -1)
WRAP_RET(int, mdb_dbi_open, (MDB_txn *a0, const char *a1, unsigned int a2, MDB_dbi *a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, mdb_stat, (MDB_txn *a0, MDB_dbi a1, MDB_stat *a2), (a0, a1, a2), -1)
WRAP_RET(int, mdb_dbi_flags, (MDB_txn *a0, MDB_dbi a1, unsigned int *a2), (a0, a1, a2), -1)
WRAP_VOID(mdb_dbi_close, (MDB_env *a0, MDB_dbi a1), (a0, a1))
WRAP_RET(int, mdb_drop, (MDB_txn *a0, MDB_dbi a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, mdb_set_compare, (MDB_txn *a0, MDB_dbi a1, MDB_cmp_func *a2), (a0, a1, a2), -1)
WRAP_RET(int, mdb_set_dupsort, (MDB_txn *a0, MDB_dbi a1, MDB_cmp_func *a2), (a0, a1, a2), -1)
WRAP_RET(int, mdb_set_relfunc, (MDB_txn *a0, MDB_dbi a1, MDB_rel_func *a2), (a0, a1, a2), -1)
WRAP_RET(int, mdb_set_relctx, (MDB_txn *a0, MDB_dbi a1, void *a2), (a0, a1, a2), -1)
WRAP_RET(int, mdb_get, (MDB_txn *a0, MDB_dbi a1, MDB_val *a2, MDB_val *a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, mdb_put, (MDB_txn *a0, MDB_dbi a1, MDB_val *a2, MDB_val *a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, mdb_del, (MDB_txn *a0, MDB_dbi a1, MDB_val *a2, MDB_val *a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, mdb_cursor_open, (MDB_txn *a0, MDB_dbi a1, MDB_cursor **a2), (a0, a1, a2), -1)
WRAP_VOID(mdb_cursor_close, (MDB_cursor *a0), (a0))
WRAP_RET(int, mdb_cursor_renew, (MDB_txn *a0, MDB_cursor *a1), (a0, a1), -1)
WRAP_RET(MDB_txn *, mdb_cursor_txn, (MDB_cursor *a0), (a0), NULL)
WRAP_RET(MDB_dbi, mdb_cursor_dbi, (MDB_cursor *a0), (a0), 0)
WRAP_RET(int, mdb_cursor_get, (MDB_cursor *a0, MDB_val *a1, MDB_val *a2, MDB_cursor_op a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, mdb_cursor_put, (MDB_cursor *a0, MDB_val *a1, MDB_val *a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, mdb_cursor_del, (MDB_cursor *a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, mdb_cursor_count, (MDB_cursor *a0, size_t *a1), (a0, a1), -1)
WRAP_RET(int, mdb_cmp, (MDB_txn *a0, MDB_dbi a1, const MDB_val *a2, const MDB_val *a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, mdb_dcmp, (MDB_txn *a0, MDB_dbi a1, const MDB_val *a2, const MDB_val *a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, mdb_reader_list, (MDB_env *a0, MDB_msg_func *a1, void *a2), (a0, a1, a2), -1)
WRAP_RET(int, mdb_reader_check, (MDB_env *a0, int *a1), (a0, a1), -1)

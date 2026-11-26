/* ============================================================
 * gdbm_dyn.c - dlopen 模拟链接：GNU dbm 1.26
 * ------------------------------------------------------------
 * 覆盖 gdbm.h 公开 API：函数 43 个 + 数据符号 4 个
 *   gdbm_version / gdbm_version_number / gdbm_errlist / gdbm_syserr
 * 44 个 _gdbm_* 内部实现符号无公开头文件声明，不封装。
 *
 * 单文件、无 .h、无 wrapper；链接期不需要 libgdbm。
 * 加载顺序：显式路径 > GDBM_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 *
 * 用法：
 *   gdbm_dyn_init(NULL);
 *   ... 原版 gdbm_* 直接调用 ...
 *   gdbm_dyn_cleanup();
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>
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

/* ---------- 类型（gdbm.h） ---------- */
typedef struct gdbm_file_info *GDBM_FILE;

typedef struct {
    char *dptr;
    int   dsize;
} datum;

typedef int gdbm_error;
typedef unsigned long long int gdbm_count_t;

struct gdbm_open_spec {
    int fd;
    int mode;
    int block_size;
    int lock_wait;
    struct timespec lock_timeout;
    struct timespec lock_interval;
    void (*fatal_func)(const char *);
};

typedef struct gdbm_recovery_s {
    void (*errfun)(void *data, char const *fmt, ...);
    void *data;
    size_t max_failed_keys;
    size_t max_failed_buckets;
    size_t max_failures;
    size_t recovered_keys;
    size_t recovered_buckets;
    size_t failed_keys;
    size_t failed_buckets;
    size_t duplicate_keys;
    char *backup_name;
} gdbm_recovery;

struct gdbm_cache_stat {
    off_t adr;
    size_t hits;
};

/* GDBM_NO_ERROR(0) .. GDBM_ERR_USAGE(44) */
#define GDBM_ERR_COUNT 45

/* ============================================================
 * 数据符号（nm 类型 D/R）：同名真实变量，加载后拷贝值
 * ============================================================ */
const char *gdbm_version = NULL;
int         gdbm_version_number[3];
const char *gdbm_errlist[GDBM_ERR_COUNT];
int         gdbm_syserr[GDBM_ERR_COUNT];

void gdbm_dyn_sync_globals(void);

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t gdbm_handle = NULL;
static int gdbm_init_state = 0;

static void *gdbm_sym(const char *name) {
    return gdbm_handle ? get_sym(gdbm_handle, name) : NULL;
}

static int gdbm_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;

    if (gdbm_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("GDBM_DYN_LIB");
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
                        snprintf(bufs[n], sizeof bufs[0], "%s/libgdbm.so.6", q);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/libgdbm.so", q);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = "libgdbm.so.6";
    candidates[n++] = "libgdbm.so";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libgdbm.so.6", home);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libgdbm.so", home);
            candidates[n] = bufs[n]; n++;
        }
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libgdbm.so.6";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libgdbm.so";
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libgdbm.so.6";
    candidates[n++] = "/usr/local/lib/libgdbm.so.6";
    candidates[n++] = "/opt/homebrew/lib/libgdbm.so";
    candidates[n++] = "/usr/local/lib/libgdbm.so";
#elif defined(_WIN32)
    candidates[n++] = "libgdbm.dll";
#else
    candidates[n++] = "/usr/local/lib/libgdbm.so.6";
    candidates[n++] = "/usr/lib/libgdbm.so.6";
    candidates[n++] = "/usr/local/lib/libgdbm.so";
    candidates[n++] = "/usr/lib/libgdbm.so";
#endif

    for (i = 0; i < n; i++) {
        gdbm_handle = load_lib(candidates[i]);
        if (gdbm_handle) break;
    }
    if (!gdbm_handle || !gdbm_sym("gdbm_open")) {
        fprintf(stderr, "gdbm_dyn: cannot load gdbm (%s)\n", lib_error());
        if (gdbm_handle) { free_lib(gdbm_handle); gdbm_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "gdbm_dyn: loaded %d %s (1.26 headers)\n", i, candidates[i]);
    gdbm_dyn_sync_globals();
    return 0;
}

int gdbm_libok(void) {
    if (gdbm_init_state == 0)
        gdbm_init_state = (gdbm_do_load(NULL) == 0) ? 1 : -1;
    return (gdbm_init_state == 1 && gdbm_handle != NULL) ? 1 : 0;
}

int gdbm_dyn_init(const char *library_path) {
    if (gdbm_do_load(library_path) == 0) {
        gdbm_init_state = 1;
        return 0;
    }
    if (gdbm_init_state == 0) gdbm_init_state = -1;
    return -1;
}

void gdbm_dyn_cleanup(void) {
    /* 仅复位加载状态，不卸载已成功加载的库（dlclose 不安全） */
    if (!gdbm_handle) gdbm_init_state = 0;
    else if (gdbm_init_state == -1) gdbm_init_state = 0;
}

int gdbm_dyn_has_symbol(const char *name) {
    if (!gdbm_libok()) return 0;
    return gdbm_sym(name) != NULL;
}

/* ============================================================
 * 数据符号值同步
 * ============================================================ */
void gdbm_dyn_sync_globals(void) {
    void *p;
    if ((p = gdbm_sym("gdbm_version")) != NULL)
        gdbm_version = *(const char **)p;
    if ((p = gdbm_sym("gdbm_version_number")) != NULL)
        memcpy(gdbm_version_number, p, sizeof gdbm_version_number);
    if ((p = gdbm_sym("gdbm_errlist")) != NULL)
        memcpy(gdbm_errlist, p, sizeof gdbm_errlist);
    if ((p = gdbm_sym("gdbm_syserr")) != NULL)
        memcpy(gdbm_syserr, p, sizeof gdbm_syserr);
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                          \
    static ret (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!gdbm_libok()) return fallback;                                \
        *(void **)(&fn) = gdbm_sym(#name);                                 \
        if (!fn) {                                                         \
            fprintf(stderr, "gdbm_dyn: dlsym failed: %s\n", #name);        \
            return fallback;                                               \
        }                                                                  \
    }                                                                      \
    return fn args;                                                        \
}

#define WRAP_VOID(name, params, args)                                      \
void name params {                                                         \
    static void (*fn) params = NULL;                                       \
    if (!fn) {                                                             \
        if (!gdbm_libok()) return;                                         \
        *(void **)(&fn) = gdbm_sym(#name);                                 \
        if (!fn) {                                                         \
            fprintf(stderr, "gdbm_dyn: dlsym failed: %s\n", #name);        \
            return;                                                        \
        }                                                                  \
    }                                                                      \
    fn args;                                                               \
}

/* ============================================================
 * 导出函数封装（gdbm.h 公开 API 43 个）
 * ============================================================ */
WRAP_RET(int, gdbm_avail_verify, (GDBM_FILE a0), (a0), -1)
WRAP_RET(int, gdbm_bucket_count, (GDBM_FILE a0, size_t *a1), (a0, a1), -1)
WRAP_RET(int, gdbm_check_syserr, (gdbm_error a0), (a0), -1)
WRAP_VOID(gdbm_clear_error, (GDBM_FILE a0), (a0))
WRAP_RET(int, gdbm_close, (GDBM_FILE a0), (a0), -1)
WRAP_RET(int, gdbm_convert, (GDBM_FILE a0, int a1), (a0, a1), -1)
WRAP_RET(int, gdbm_copy_meta, (GDBM_FILE a0, GDBM_FILE a1), (a0, a1), -1)
WRAP_RET(int, gdbm_count, (GDBM_FILE a0, gdbm_count_t *a1), (a0, a1), -1)
WRAP_RET(const char *, gdbm_db_strerror, (GDBM_FILE a0), (a0), NULL)
WRAP_RET(int, gdbm_delete, (GDBM_FILE a0, datum a1), (a0, a1), -1)
WRAP_RET(int, gdbm_dump, (GDBM_FILE a0, const char *a1, int a2, int a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gdbm_dump_to_file, (GDBM_FILE a0, FILE *a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int *, gdbm_errno_location, (void), (), NULL)
WRAP_RET(int, gdbm_exists, (GDBM_FILE a0, datum a1), (a0, a1), -1)
WRAP_RET(int, gdbm_export, (GDBM_FILE a0, const char *a1, int a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gdbm_export_to_file, (GDBM_FILE a0, FILE *a1), (a0, a1), -1)
WRAP_RET(int, gdbm_failure_atomic, (GDBM_FILE a0, const char *a1, const char *a2), (a0, a1, a2), -1)
WRAP_RET(int, gdbm_fdesc, (GDBM_FILE a0), (a0), -1)
WRAP_RET(GDBM_FILE, gdbm_fd_open, (int a0, const char *a1, int a2, int a3, void (*a4)(const char *)), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(datum, gdbm_fetch, (GDBM_FILE a0, datum a1), (a0, a1), ((datum){NULL, 0}))
WRAP_RET(datum, gdbm_firstkey, (GDBM_FILE a0), (a0), ((datum){NULL, 0}))
WRAP_VOID(gdbm_get_cache_stats, (GDBM_FILE a0, size_t *a1, size_t *a2, size_t *a3, struct gdbm_cache_stat *a4, size_t a5), (a0, a1, a2, a3, a4, a5))
WRAP_RET(int, gdbm_import, (GDBM_FILE a0, const char *a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, gdbm_import_from_file, (GDBM_FILE a0, FILE *a1, int a2), (a0, a1, a2), -1)
WRAP_RET(gdbm_error, gdbm_last_errno, (GDBM_FILE a0), (a0), 0)
WRAP_RET(int, gdbm_last_syserr, (GDBM_FILE a0), (a0), 0)
WRAP_RET(int, gdbm_latest_snapshot, (const char *a0, const char *a1, const char **a2), (a0, a1, a2), -1)
WRAP_RET(int, gdbm_load, (GDBM_FILE *a0, const char *a1, int a2, int a3, unsigned long *a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gdbm_load_from_file, (GDBM_FILE *a0, FILE *a1, int a2, int a3, unsigned long *a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gdbm_load_from_file_ext, (GDBM_FILE *a0, FILE *a1, int a2, int a3, int a4, unsigned long *a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gdbm_needs_recovery, (GDBM_FILE a0), (a0), -1)
WRAP_RET(datum, gdbm_nextkey, (GDBM_FILE a0, datum a1), (a0, a1), ((datum){NULL, 0}))
WRAP_RET(GDBM_FILE, gdbm_open, (const char *a0, int a1, int a2, int a3, void (*a4)(const char *)), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(GDBM_FILE, gdbm_open_ext, (const char *a0, int a1, const struct gdbm_open_spec *a2), (a0, a1, a2), NULL)
WRAP_VOID(gdbm_open_spec_init, (struct gdbm_open_spec *a0), (a0))
WRAP_RET(int, gdbm_recover, (GDBM_FILE a0, gdbm_recovery *a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, gdbm_reorganize, (GDBM_FILE a0), (a0), -1)
WRAP_VOID(gdbm_set_errno, (GDBM_FILE a0, gdbm_error a1, int a2), (a0, a1, a2))
WRAP_RET(int, gdbm_setopt, (GDBM_FILE a0, int a1, void *a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gdbm_store, (GDBM_FILE a0, datum a1, datum a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(const char *, gdbm_strerror, (gdbm_error a0), (a0), NULL)
WRAP_RET(int, gdbm_sync, (GDBM_FILE a0), (a0), -1)
WRAP_RET(int, gdbm_version_cmp, (const int *a0, const int *a1), (a0, a1), -1)

/* ============================================================
 * leveldb_dyn.c - dlopen 模拟链接：LevelDB 1.23 C API
 * ------------------------------------------------------------
 * 覆盖 leveldb/c.h 全部 68 个 leveldb_* 导出函数。
 * libleveldb.so 另有 107 个 C++ mangled 符号（Slice/std::string 按值
 * ABI），不属于稳定 C 接口，不封装。
 * 无数据符号（nm -D 只有 T）。
 *
 * 单文件、无 .h、无 wrapper；链接期不需要 libleveldb。
 * 加载顺序：显式路径 > LEVELDB_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 *
 * 用法：
 *   leveldb_dyn_init(NULL);
 *   ... 原版 leveldb_* 直接调用 ...
 *   leveldb_dyn_cleanup();
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
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
#  if __has_include(<leveldb/c.h>)
#    include <leveldb/c.h>
#    define LEVELDB_DYN_HAVE_HEADER 1
#  endif
#endif

/* ---------- 类型（leveldb/c.h） ---------- */
#ifndef LEVELDB_DYN_HAVE_HEADER
typedef struct leveldb_t leveldb_t;
typedef struct leveldb_cache_t leveldb_cache_t;
typedef struct leveldb_comparator_t leveldb_comparator_t;
typedef struct leveldb_env_t leveldb_env_t;
typedef struct leveldb_filelock_t leveldb_filelock_t;
typedef struct leveldb_filterpolicy_t leveldb_filterpolicy_t;
typedef struct leveldb_iterator_t leveldb_iterator_t;
typedef struct leveldb_logger_t leveldb_logger_t;
typedef struct leveldb_options_t leveldb_options_t;
typedef struct leveldb_randomfile_t leveldb_randomfile_t;
typedef struct leveldb_readoptions_t leveldb_readoptions_t;
typedef struct leveldb_seqfile_t leveldb_seqfile_t;
typedef struct leveldb_snapshot_t leveldb_snapshot_t;
typedef struct leveldb_writablefile_t leveldb_writablefile_t;
typedef struct leveldb_writebatch_t leveldb_writebatch_t;
typedef struct leveldb_writeoptions_t leveldb_writeoptions_t;
#endif /* LEVELDB_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t leveldb_handle = NULL;
static int leveldb_init_state = 0;

static void *leveldb_sym(const char *name) {
    return leveldb_handle ? get_sym(leveldb_handle, name) : NULL;
}

static int leveldb_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;

    if (leveldb_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("LEVELDB_DYN_LIB");
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
                        snprintf(bufs[n], sizeof bufs[0], "%s/libleveldb.so.1", q);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/libleveldb.so", q);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = "libleveldb.so.1";
    candidates[n++] = "libleveldb.so";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libleveldb.so.1", home);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libleveldb.so", home);
            candidates[n] = bufs[n]; n++;
        }
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libleveldb.so.1";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libleveldb.so";
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libleveldb.so.1";
    candidates[n++] = "/usr/local/lib/libleveldb.so.1";
    candidates[n++] = "/opt/homebrew/lib/libleveldb.so";
    candidates[n++] = "/usr/local/lib/libleveldb.so";
#elif defined(_WIN32)
    candidates[n++] = "libleveldb.dll";
#else
    candidates[n++] = "/usr/local/lib/libleveldb.so.1";
    candidates[n++] = "/usr/lib/libleveldb.so.1";
    candidates[n++] = "/usr/local/lib/libleveldb.so";
    candidates[n++] = "/usr/lib/libleveldb.so";
#endif

    for (i = 0; i < n; i++) {
        leveldb_handle = load_lib(candidates[i]);
        if (leveldb_handle) break;
    }
    if (!leveldb_handle || !leveldb_sym("leveldb_open")) {
        fprintf(stderr, "leveldb_dyn: cannot load leveldb (%s)\n", lib_error());
        if (leveldb_handle) { free_lib(leveldb_handle); leveldb_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "leveldb_dyn: loaded leveldb (1.23 headers)\n");
    return 0;
}

int leveldb_dyn_libok(void) {
    if (leveldb_init_state == 0)
        leveldb_init_state = (leveldb_do_load(NULL) == 0) ? 1 : -1;
    return (leveldb_init_state == 1 && leveldb_handle != NULL) ? 1 : 0;
}

int leveldb_dyn_init(const char *library_path) {
    if (leveldb_do_load(library_path) == 0) {
        leveldb_init_state = 1;
        return 0;
    }
    if (leveldb_init_state == 0) leveldb_init_state = -1;
    return -1;
}

void leveldb_dyn_cleanup(void) {
    /* 仅复位加载状态，不卸载已成功加载的库（dlclose 不安全） */
    if (!leveldb_handle) leveldb_init_state = 0;
    else if (leveldb_init_state == -1) leveldb_init_state = 0;
}

int leveldb_dyn_has_symbol(const char *name) {
    if (!leveldb_dyn_libok()) return 0;
    return leveldb_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                          \
    static ret (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!leveldb_dyn_libok()) return fallback;                         \
        *(void **)(&fn) = leveldb_sym(#name);                              \
        if (!fn) {                                                         \
            fprintf(stderr, "leveldb_dyn: dlsym failed: %s\n", #name);     \
            return fallback;                                               \
        }                                                                  \
    }                                                                      \
    return fn args;                                                        \
}

#define WRAP_VOID(name, params, args)                                      \
void name params {                                                         \
    static void (*fn) params = NULL;                                       \
    if (!fn) {                                                             \
        if (!leveldb_dyn_libok()) return;                                  \
        *(void **)(&fn) = leveldb_sym(#name);                              \
        if (!fn) {                                                         \
            fprintf(stderr, "leveldb_dyn: dlsym failed: %s\n", #name);     \
            return;                                                        \
        }                                                                  \
    }                                                                      \
    fn args;                                                               \
}

/* ============================================================
 * 导出函数封装（leveldb/c.h 68 个）
 * ============================================================ */
/* DB operations */
WRAP_RET(leveldb_t *, leveldb_open, (const leveldb_options_t *a0, const char *a1, char **a2), (a0, a1, a2), NULL)
WRAP_VOID(leveldb_close, (leveldb_t *a0), (a0))
WRAP_VOID(leveldb_put, (leveldb_t *a0, const leveldb_writeoptions_t *a1, const char *a2, size_t a3, const char *a4, size_t a5, char **a6), (a0, a1, a2, a3, a4, a5, a6))
WRAP_VOID(leveldb_delete, (leveldb_t *a0, const leveldb_writeoptions_t *a1, const char *a2, size_t a3, char **a4), (a0, a1, a2, a3, a4))
WRAP_VOID(leveldb_write, (leveldb_t *a0, const leveldb_writeoptions_t *a1, leveldb_writebatch_t *a2, char **a3), (a0, a1, a2, a3))
WRAP_RET(char *, leveldb_get, (leveldb_t *a0, const leveldb_readoptions_t *a1, const char *a2, size_t a3, size_t *a4, char **a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(leveldb_iterator_t *, leveldb_create_iterator, (leveldb_t *a0, const leveldb_readoptions_t *a1), (a0, a1), NULL)
WRAP_RET(const leveldb_snapshot_t *, leveldb_create_snapshot, (leveldb_t *a0), (a0), NULL)
WRAP_VOID(leveldb_release_snapshot, (leveldb_t *a0, const leveldb_snapshot_t *a1), (a0, a1))
WRAP_RET(char *, leveldb_property_value, (leveldb_t *a0, const char *a1), (a0, a1), NULL)
WRAP_VOID(leveldb_approximate_sizes, (leveldb_t *a0, int a1, const char *const *a2, const size_t *a3, const char *const *a4, const size_t *a5, uint64_t *a6), (a0, a1, a2, a3, a4, a5, a6))
WRAP_VOID(leveldb_compact_range, (leveldb_t *a0, const char *a1, size_t a2, const char *a3, size_t a4), (a0, a1, a2, a3, a4))
WRAP_VOID(leveldb_destroy_db, (const leveldb_options_t *a0, const char *a1, char **a2), (a0, a1, a2))
WRAP_VOID(leveldb_repair_db, (const leveldb_options_t *a0, const char *a1, char **a2), (a0, a1, a2))

/* Iterator */
WRAP_VOID(leveldb_iter_destroy, (leveldb_iterator_t *a0), (a0))
WRAP_RET(unsigned char, leveldb_iter_valid, (const leveldb_iterator_t *a0), (a0), 0)
WRAP_VOID(leveldb_iter_seek_to_first, (leveldb_iterator_t *a0), (a0))
WRAP_VOID(leveldb_iter_seek_to_last, (leveldb_iterator_t *a0), (a0))
WRAP_VOID(leveldb_iter_seek, (leveldb_iterator_t *a0, const char *a1, size_t a2), (a0, a1, a2))
WRAP_VOID(leveldb_iter_next, (leveldb_iterator_t *a0), (a0))
WRAP_VOID(leveldb_iter_prev, (leveldb_iterator_t *a0), (a0))
WRAP_RET(const char *, leveldb_iter_key, (const leveldb_iterator_t *a0, size_t *a1), (a0, a1), NULL)
WRAP_RET(const char *, leveldb_iter_value, (const leveldb_iterator_t *a0, size_t *a1), (a0, a1), NULL)
WRAP_VOID(leveldb_iter_get_error, (const leveldb_iterator_t *a0, char **a1), (a0, a1))

/* Write batch */
WRAP_RET(leveldb_writebatch_t *, leveldb_writebatch_create, (void), (), NULL)
WRAP_VOID(leveldb_writebatch_destroy, (leveldb_writebatch_t *a0), (a0))
WRAP_VOID(leveldb_writebatch_clear, (leveldb_writebatch_t *a0), (a0))
WRAP_VOID(leveldb_writebatch_put, (leveldb_writebatch_t *a0, const char *a1, size_t a2, const char *a3, size_t a4), (a0, a1, a2, a3, a4))
WRAP_VOID(leveldb_writebatch_delete, (leveldb_writebatch_t *a0, const char *a1, size_t a2), (a0, a1, a2))
WRAP_VOID(leveldb_writebatch_iterate, (const leveldb_writebatch_t *a0, void *a1, void (*a2)(void *, const char *, size_t, const char *, size_t), void (*a3)(void *, const char *, size_t)), (a0, a1, a2, a3))
WRAP_VOID(leveldb_writebatch_append, (leveldb_writebatch_t *a0, const leveldb_writebatch_t *a1), (a0, a1))

/* Options */
WRAP_RET(leveldb_options_t *, leveldb_options_create, (void), (), NULL)
WRAP_VOID(leveldb_options_destroy, (leveldb_options_t *a0), (a0))
WRAP_VOID(leveldb_options_set_comparator, (leveldb_options_t *a0, leveldb_comparator_t *a1), (a0, a1))
WRAP_VOID(leveldb_options_set_filter_policy, (leveldb_options_t *a0, leveldb_filterpolicy_t *a1), (a0, a1))
WRAP_VOID(leveldb_options_set_create_if_missing, (leveldb_options_t *a0, unsigned char a1), (a0, a1))
WRAP_VOID(leveldb_options_set_error_if_exists, (leveldb_options_t *a0, unsigned char a1), (a0, a1))
WRAP_VOID(leveldb_options_set_paranoid_checks, (leveldb_options_t *a0, unsigned char a1), (a0, a1))
WRAP_VOID(leveldb_options_set_env, (leveldb_options_t *a0, leveldb_env_t *a1), (a0, a1))
WRAP_VOID(leveldb_options_set_info_log, (leveldb_options_t *a0, leveldb_logger_t *a1), (a0, a1))
WRAP_VOID(leveldb_options_set_write_buffer_size, (leveldb_options_t *a0, size_t a1), (a0, a1))
WRAP_VOID(leveldb_options_set_max_open_files, (leveldb_options_t *a0, int a1), (a0, a1))
WRAP_VOID(leveldb_options_set_cache, (leveldb_options_t *a0, leveldb_cache_t *a1), (a0, a1))
WRAP_VOID(leveldb_options_set_block_size, (leveldb_options_t *a0, size_t a1), (a0, a1))
WRAP_VOID(leveldb_options_set_block_restart_interval, (leveldb_options_t *a0, int a1), (a0, a1))
WRAP_VOID(leveldb_options_set_max_file_size, (leveldb_options_t *a0, size_t a1), (a0, a1))
WRAP_VOID(leveldb_options_set_compression, (leveldb_options_t *a0, int a1), (a0, a1))

/* Comparator */
WRAP_RET(leveldb_comparator_t *, leveldb_comparator_create, (void *a0, void (*a1)(void *), int (*a2)(void *, const char *, size_t, const char *, size_t), const char *(*a3)(void *)), (a0, a1, a2, a3), NULL)
WRAP_VOID(leveldb_comparator_destroy, (leveldb_comparator_t *a0), (a0))

/* Filter policy */
WRAP_RET(leveldb_filterpolicy_t *, leveldb_filterpolicy_create, (void *a0, void (*a1)(void *), char *(*a2)(void *, const char *const *, const size_t *, int, size_t *), unsigned char (*a3)(void *, const char *, size_t, const char *, size_t), const char *(*a4)(void *)), (a0, a1, a2, a3, a4), NULL)
WRAP_VOID(leveldb_filterpolicy_destroy, (leveldb_filterpolicy_t *a0), (a0))
WRAP_RET(leveldb_filterpolicy_t *, leveldb_filterpolicy_create_bloom, (int a0), (a0), NULL)

/* Read options */
WRAP_RET(leveldb_readoptions_t *, leveldb_readoptions_create, (void), (), NULL)
WRAP_VOID(leveldb_readoptions_destroy, (leveldb_readoptions_t *a0), (a0))
WRAP_VOID(leveldb_readoptions_set_verify_checksums, (leveldb_readoptions_t *a0, unsigned char a1), (a0, a1))
WRAP_VOID(leveldb_readoptions_set_fill_cache, (leveldb_readoptions_t *a0, unsigned char a1), (a0, a1))
WRAP_VOID(leveldb_readoptions_set_snapshot, (leveldb_readoptions_t *a0, const leveldb_snapshot_t *a1), (a0, a1))

/* Write options */
WRAP_RET(leveldb_writeoptions_t *, leveldb_writeoptions_create, (void), (), NULL)
WRAP_VOID(leveldb_writeoptions_destroy, (leveldb_writeoptions_t *a0), (a0))
WRAP_VOID(leveldb_writeoptions_set_sync, (leveldb_writeoptions_t *a0, unsigned char a1), (a0, a1))

/* Cache / Env / Utility */
WRAP_RET(leveldb_cache_t *, leveldb_cache_create_lru, (size_t a0), (a0), NULL)
WRAP_VOID(leveldb_cache_destroy, (leveldb_cache_t *a0), (a0))
WRAP_RET(leveldb_env_t *, leveldb_create_default_env, (void), (), NULL)
WRAP_VOID(leveldb_env_destroy, (leveldb_env_t *a0), (a0))
WRAP_RET(char *, leveldb_env_get_test_directory, (leveldb_env_t *a0), (a0), NULL)
WRAP_VOID(leveldb_free, (void *a0), (a0))
WRAP_RET(int, leveldb_major_version, (void), (), -1)
WRAP_RET(int, leveldb_minor_version, (void), (), -1)

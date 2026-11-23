/* leveldb_dyn 测试：只链 -ldl，不链 libleveldb
 * 覆盖：版本、options、open、put/get/delete、writebatch、iterator、
 *       snapshot、property、approximate_sizes、compact_range、destroy_db，
 *       以及 68 个导出符号的 has_symbol 探测。
 */
#include "test_common.h"
#include <stdint.h>

typedef struct leveldb_t leveldb_t;
typedef struct leveldb_cache_t leveldb_cache_t;
typedef struct leveldb_comparator_t leveldb_comparator_t;
typedef struct leveldb_env_t leveldb_env_t;
typedef struct leveldb_filterpolicy_t leveldb_filterpolicy_t;
typedef struct leveldb_iterator_t leveldb_iterator_t;
typedef struct leveldb_options_t leveldb_options_t;
typedef struct leveldb_readoptions_t leveldb_readoptions_t;
typedef struct leveldb_snapshot_t leveldb_snapshot_t;
typedef struct leveldb_writebatch_t leveldb_writebatch_t;
typedef struct leveldb_writeoptions_t leveldb_writeoptions_t;

extern int  leveldb_dyn_init(const char *);
extern int  leveldb_dyn_libok(void);
extern void leveldb_dyn_cleanup(void);
extern int  leveldb_dyn_has_symbol(const char *);

extern leveldb_t *leveldb_open(const leveldb_options_t *, const char *, char **);
extern void leveldb_close(leveldb_t *);
extern void leveldb_put(leveldb_t *, const leveldb_writeoptions_t *, const char *, size_t,
                        const char *, size_t, char **);
extern void leveldb_delete(leveldb_t *, const leveldb_writeoptions_t *, const char *, size_t,
                           char **);
extern void leveldb_write(leveldb_t *, const leveldb_writeoptions_t *,
                          leveldb_writebatch_t *, char **);
extern char *leveldb_get(leveldb_t *, const leveldb_readoptions_t *, const char *, size_t,
                         size_t *, char **);
extern leveldb_iterator_t *leveldb_create_iterator(leveldb_t *, const leveldb_readoptions_t *);
extern const leveldb_snapshot_t *leveldb_create_snapshot(leveldb_t *);
extern void leveldb_release_snapshot(leveldb_t *, const leveldb_snapshot_t *);
extern char *leveldb_property_value(leveldb_t *, const char *);
extern void leveldb_approximate_sizes(leveldb_t *, int, const char *const *, const size_t *,
                                      const char *const *, const size_t *, uint64_t *);
extern void leveldb_compact_range(leveldb_t *, const char *, size_t, const char *, size_t);
extern void leveldb_destroy_db(const leveldb_options_t *, const char *, char **);
extern void leveldb_repair_db(const leveldb_options_t *, const char *, char **);

extern void leveldb_iter_destroy(leveldb_iterator_t *);
extern unsigned char leveldb_iter_valid(const leveldb_iterator_t *);
extern void leveldb_iter_seek_to_first(leveldb_iterator_t *);
extern void leveldb_iter_next(leveldb_iterator_t *);
extern const char *leveldb_iter_key(const leveldb_iterator_t *, size_t *);
extern const char *leveldb_iter_value(const leveldb_iterator_t *, size_t *);

extern leveldb_writebatch_t *leveldb_writebatch_create(void);
extern void leveldb_writebatch_destroy(leveldb_writebatch_t *);
extern void leveldb_writebatch_put(leveldb_writebatch_t *, const char *, size_t,
                                   const char *, size_t);
extern void leveldb_writebatch_clear(leveldb_writebatch_t *);

extern leveldb_options_t *leveldb_options_create(void);
extern void leveldb_options_destroy(leveldb_options_t *);
extern void leveldb_options_set_create_if_missing(leveldb_options_t *, unsigned char);
extern void leveldb_options_set_write_buffer_size(leveldb_options_t *, size_t);
extern void leveldb_options_set_max_open_files(leveldb_options_t *, int);
extern void leveldb_options_set_compression(leveldb_options_t *, int);

extern leveldb_readoptions_t *leveldb_readoptions_create(void);
extern void leveldb_readoptions_destroy(leveldb_readoptions_t *);
extern void leveldb_readoptions_set_fill_cache(leveldb_readoptions_t *, unsigned char);
extern void leveldb_readoptions_set_snapshot(leveldb_readoptions_t *,
                                             const leveldb_snapshot_t *);

extern leveldb_writeoptions_t *leveldb_writeoptions_create(void);
extern void leveldb_writeoptions_destroy(leveldb_writeoptions_t *);
extern void leveldb_writeoptions_set_sync(leveldb_writeoptions_t *, unsigned char);

extern leveldb_cache_t *leveldb_cache_create_lru(size_t);
extern void leveldb_cache_destroy(leveldb_cache_t *);
extern void leveldb_options_set_cache(leveldb_options_t *, leveldb_cache_t *);
extern leveldb_env_t *leveldb_create_default_env(void);
extern void leveldb_env_destroy(leveldb_env_t *);
extern char *leveldb_env_get_test_directory(leveldb_env_t *);
extern void leveldb_free(void *);
extern int leveldb_major_version(void);
extern int leveldb_minor_version(void);

static const char *const k_syms[] = {
    "leveldb_open", "leveldb_close", "leveldb_put", "leveldb_delete", "leveldb_write",
    "leveldb_get", "leveldb_create_iterator", "leveldb_create_snapshot",
    "leveldb_release_snapshot", "leveldb_property_value", "leveldb_approximate_sizes",
    "leveldb_compact_range", "leveldb_destroy_db", "leveldb_repair_db",
    "leveldb_iter_destroy", "leveldb_iter_valid", "leveldb_iter_seek_to_first",
    "leveldb_iter_seek_to_last", "leveldb_iter_seek", "leveldb_iter_next",
    "leveldb_iter_prev", "leveldb_iter_key", "leveldb_iter_value", "leveldb_iter_get_error",
    "leveldb_writebatch_create", "leveldb_writebatch_destroy", "leveldb_writebatch_clear",
    "leveldb_writebatch_put", "leveldb_writebatch_delete", "leveldb_writebatch_iterate",
    "leveldb_writebatch_append", "leveldb_options_create", "leveldb_options_destroy",
    "leveldb_options_set_comparator", "leveldb_options_set_filter_policy",
    "leveldb_options_set_create_if_missing", "leveldb_options_set_error_if_exists",
    "leveldb_options_set_paranoid_checks", "leveldb_options_set_env",
    "leveldb_options_set_info_log", "leveldb_options_set_write_buffer_size",
    "leveldb_options_set_max_open_files", "leveldb_options_set_cache",
    "leveldb_options_set_block_size", "leveldb_options_set_block_restart_interval",
    "leveldb_options_set_max_file_size", "leveldb_options_set_compression",
    "leveldb_comparator_create", "leveldb_comparator_destroy",
    "leveldb_filterpolicy_create", "leveldb_filterpolicy_destroy",
    "leveldb_filterpolicy_create_bloom", "leveldb_readoptions_create",
    "leveldb_readoptions_destroy", "leveldb_readoptions_set_verify_checksums",
    "leveldb_readoptions_set_fill_cache", "leveldb_readoptions_set_snapshot",
    "leveldb_writeoptions_create", "leveldb_writeoptions_destroy",
    "leveldb_writeoptions_set_sync", "leveldb_cache_create_lru", "leveldb_cache_destroy",
    "leveldb_create_default_env", "leveldb_env_destroy", "leveldb_env_get_test_directory",
    "leveldb_free", "leveldb_major_version", "leveldb_minor_version"
};

static void free_err(char **e) {
    if (*e) {
        printf("leveldb error: %s\n", *e);
        leveldb_free(*e);
        *e = NULL;
    }
}

int main(int argc, char **argv) {
    int failures = 0, i, bad = 0, n;
    const char *path = "/tmp/rtdb_leveldb_test";
    leveldb_t *db = NULL;
    leveldb_options_t *opt;
    leveldb_readoptions_t *ropt;
    leveldb_writeoptions_t *wopt;
    leveldb_iterator_t *it;
    leveldb_writebatch_t *wb;
    const leveldb_snapshot_t *snap;
    leveldb_cache_t *cache;
    leveldb_env_t *env;
    char *err = NULL;
    char *val;
    size_t vlen = 0;

    if (leveldb_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("leveldb 不可加载\n");
    failures += CHECK(leveldb_dyn_libok() != 0);

    printf("leveldb %d.%d\n", leveldb_major_version(), leveldb_minor_version());
    failures += CHECK(leveldb_major_version() > 0);

    opt = leveldb_options_create();
    failures += CHECK(opt != NULL);
    leveldb_options_set_create_if_missing(opt, 1);
    leveldb_options_set_write_buffer_size(opt, (size_t)1 << 20);
    leveldb_options_set_max_open_files(opt, 64);
    leveldb_options_set_compression(opt, 0);
    cache = leveldb_cache_create_lru((size_t)4 << 20);
    failures += CHECK(cache != NULL);
    leveldb_options_set_cache(opt, cache);

    leveldb_destroy_db(opt, path, &err);
    free_err(&err);

    db = leveldb_open(opt, path, &err);
    if (err) printf("open: %s\n", err);
    failures += CHECK(db != NULL && err == NULL);
    free_err(&err);
    if (!db) { printf("%d checks failed\n", failures); return 1; }

    ropt = leveldb_readoptions_create();
    wopt = leveldb_writeoptions_create();
    failures += CHECK(ropt != NULL && wopt != NULL);

    /* put / get / delete */
    leveldb_put(db, wopt, "a", 1, "1", 1, &err);
    failures += CHECK(err == NULL);
    free_err(&err);
    leveldb_put(db, wopt, "b", 1, "2", 1, &err);
    failures += CHECK(err == NULL);
    free_err(&err);

    val = leveldb_get(db, ropt, "a", 1, &vlen, &err);
    failures += CHECK(val != NULL && vlen == 1 && memcmp(val, "1", 1) == 0);
    leveldb_free(val);
    free_err(&err);

    leveldb_delete(db, wopt, "b", 1, &err);
    failures += CHECK(err == NULL);
    free_err(&err);

    /* writebatch */
    wb = leveldb_writebatch_create();
    failures += CHECK(wb != NULL);
    leveldb_writebatch_put(wb, "c", 1, "3", 1);
    leveldb_writebatch_put(wb, "d", 1, "4", 1);
    leveldb_write(db, wopt, wb, &err);
    failures += CHECK(err == NULL);
    free_err(&err);
    leveldb_writebatch_clear(wb);
    leveldb_writebatch_destroy(wb);

    /* iterator */
    it = leveldb_create_iterator(db, ropt);
    failures += CHECK(it != NULL);
    n = 0;
    for (leveldb_iter_seek_to_first(it); leveldb_iter_valid(it); leveldb_iter_next(it)) {
        size_t klen = 0;
        const char *k = leveldb_iter_key(it, &klen);
        n++;
        if (k && klen == 1) printf("key=%c\n", k[0]);
    }
    printf("iterated keys: %d\n", n);
    failures += CHECK(n == 3);
    leveldb_iter_destroy(it);

    /* snapshot + property */
    snap = leveldb_create_snapshot(db);
    failures += CHECK(snap != NULL);
    leveldb_readoptions_set_snapshot(ropt, snap);
    val = leveldb_get(db, ropt, "a", 1, &vlen, &err);
    failures += CHECK(val != NULL);
    leveldb_free(val);
    free_err(&err);
    leveldb_release_snapshot(db, snap);
    leveldb_readoptions_set_fill_cache(ropt, 1);

    val = leveldb_property_value(db, "leveldb.stats");
    printf("property len: %zu\n", val ? strlen(val) : (size_t)0);
    failures += CHECK(val != NULL);
    leveldb_free(val);

    /* compact_range 先落盘，再 approximate_sizes */
    leveldb_compact_range(db, "a", 1, "z", 1);
    {
        const char *s = "a", *l = "z";
        size_t sl = 1, ll = 1;
        uint64_t sizes[1] = {0};
        leveldb_approximate_sizes(db, 1, &s, &sl, &l, &ll, sizes);
        printf("approximate size: %llu\n", (unsigned long long)sizes[0]);
        failures += CHECK(sizes[0] > 0);
    }

    leveldb_writeoptions_set_sync(wopt, 1);
    leveldb_close(db);
    db = NULL;

    leveldb_repair_db(opt, path, &err);
    free_err(&err);
    leveldb_destroy_db(opt, path, &err);
    free_err(&err);

    /* env */
    env = leveldb_create_default_env();
    failures += CHECK(env != NULL);
    if (env) {
        char *dir = leveldb_env_get_test_directory(env);
        printf("env test dir: %s\n", dir ? dir : "(null)");
        leveldb_free(dir);
        leveldb_env_destroy(env);
    }

    leveldb_readoptions_destroy(ropt);
    leveldb_writeoptions_destroy(wopt);
    leveldb_cache_destroy(cache);
    leveldb_options_destroy(opt);

    /* 符号探测（68） */
    for (i = 0; i < (int)(sizeof k_syms / sizeof k_syms[0]); i++) {
        if (!leveldb_dyn_has_symbol(k_syms[i])) {
            printf("missing symbol: %s\n", k_syms[i]);
            bad++;
        }
    }
    printf("symbols: %d checked, %d missing\n",
           (int)(sizeof k_syms / sizeof k_syms[0]), bad);
    failures += CHECK(bad == 0);
    failures += CHECK((int)(sizeof k_syms / sizeof k_syms[0]) == 68);

    leveldb_dyn_cleanup();
    if (failures) { printf("%d checks failed\n", failures); return 1; }
    printf("all checks passed\n");
    return 0;
}

/* gdbm_dyn 测试：只链 -ldl，不链 libgdbm
 * 覆盖：版本/数据符号、建库、存取删、exists、count、键遍历、
 *       setopt/sync/reorganize、dump、export/import、错误码，
 *       以及 43 函数 + 4 数据符号的 has_symbol 探测。
 */
#include "test_common.h"
#include <sys/types.h>

typedef struct gdbm_file_info *GDBM_FILE;
typedef struct { char *dptr; int dsize; } datum;
typedef unsigned long long int gdbm_count_t;

#define GDBM_READER         0
#define GDBM_NEWDB          3
#define GDBM_REPLACE        1
#define GDBM_SETCACHESIZE   1
#define GDBM_DUMP_FMT_ASCII 1

extern int  gdbm_dyn_init(const char *);
extern int  gdbm_libok(void);
extern void gdbm_dyn_cleanup(void);
extern int  gdbm_dyn_has_symbol(const char *);

extern GDBM_FILE gdbm_open(const char *, int, int, int, void (*)(const char *));
extern int       gdbm_close(GDBM_FILE);
extern int       gdbm_store(GDBM_FILE, datum, datum, int);
extern datum     gdbm_fetch(GDBM_FILE, datum);
extern int       gdbm_delete(GDBM_FILE, datum);
extern datum     gdbm_firstkey(GDBM_FILE);
extern datum     gdbm_nextkey(GDBM_FILE, datum);
extern int       gdbm_exists(GDBM_FILE, datum);
extern int       gdbm_count(GDBM_FILE, gdbm_count_t *);
extern int       gdbm_reorganize(GDBM_FILE);
extern int       gdbm_sync(GDBM_FILE);
extern int       gdbm_setopt(GDBM_FILE, int, void *, int);
extern int       gdbm_dump_to_file(GDBM_FILE, FILE *, int);
extern int       gdbm_export_to_file(GDBM_FILE, FILE *);
extern int       gdbm_import_from_file(GDBM_FILE, FILE *, int);
extern int       gdbm_needs_recovery(GDBM_FILE);
extern int       gdbm_fdesc(GDBM_FILE);
extern const char *gdbm_strerror(int);
extern int       *gdbm_errno_location(void);

extern const char *gdbm_version;
extern int         gdbm_version_number[3];
extern const char *gdbm_errlist[];
extern int         gdbm_syserr[];

static const char *const k_syms[] = {
    "gdbm_avail_verify", "gdbm_bucket_count", "gdbm_check_syserr", "gdbm_clear_error",
    "gdbm_close", "gdbm_convert", "gdbm_copy_meta", "gdbm_count", "gdbm_db_strerror",
    "gdbm_delete", "gdbm_dump", "gdbm_dump_to_file", "gdbm_errno_location", "gdbm_exists",
    "gdbm_export", "gdbm_export_to_file", "gdbm_failure_atomic", "gdbm_fdesc",
    "gdbm_fd_open", "gdbm_fetch", "gdbm_firstkey", "gdbm_get_cache_stats", "gdbm_import",
    "gdbm_import_from_file", "gdbm_last_errno", "gdbm_last_syserr", "gdbm_latest_snapshot",
    "gdbm_load", "gdbm_load_from_file", "gdbm_load_from_file_ext", "gdbm_needs_recovery",
    "gdbm_nextkey", "gdbm_open", "gdbm_open_ext", "gdbm_open_spec_init", "gdbm_recover",
    "gdbm_reorganize", "gdbm_set_errno", "gdbm_setopt", "gdbm_store", "gdbm_strerror",
    "gdbm_sync", "gdbm_version_cmp",
    "gdbm_version", "gdbm_version_number", "gdbm_errlist", "gdbm_syserr"
};

static datum mk(char *s, int n) {
    datum d;
    d.dptr = s;
    d.dsize = n;
    return d;
}

int main(int argc, char **argv) {
    int failures = 0, i, bad = 0, n;
    const char *path = "/tmp/rtdb_gdbm_test.db";
    const char *dump = "/tmp/rtdb_gdbm_test.dump";
    GDBM_FILE db, bad_db;
    datum k, v, got, key;
    gdbm_count_t cnt;
    FILE *fp;

    if (gdbm_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("gdbm 不可加载\n");
    failures += CHECK(gdbm_libok() != 0);

    /* 1. 版本与数据符号 */
    printf("gdbm version: %s (%d.%d.%d)\n", gdbm_version,
           gdbm_version_number[0], gdbm_version_number[1], gdbm_version_number[2]);
    failures += CHECK(gdbm_version != NULL);
    failures += CHECK(gdbm_version_number[0] == 1);
    failures += CHECK(gdbm_errlist[0] != NULL);
    failures += CHECK(gdbm_syserr[0] == 0);

    /* 2. 建库 + 存 */
    remove(path);
    remove(dump);
    db = gdbm_open(path, 512, GDBM_NEWDB, 0644, NULL);
    failures += CHECK(db != NULL);
    if (!db) { printf("%d checks failed\n", failures); return 1; }

    k = mk("k1", 2);
    v = mk("v1", 2);
    failures += CHECK(gdbm_store(db, k, v, GDBM_REPLACE) == 0);
    failures += CHECK(gdbm_store(db, mk("k2", 2), mk("v2", 2), GDBM_REPLACE) == 0);

    /* 3. 取 / exists / count */
    got = gdbm_fetch(db, k);
    failures += CHECK(got.dptr != NULL && got.dsize == 2 && memcmp(got.dptr, "v1", 2) == 0);
    free(got.dptr);
    failures += CHECK(gdbm_exists(db, k) != 0);

    cnt = 0;
    failures += CHECK(gdbm_count(db, &cnt) == 0);
    printf("count: %llu\n", cnt);
    failures += CHECK(cnt == 2);

    /* 4. 键遍历 */
    n = 0;
    key = gdbm_firstkey(db);
    while (key.dptr) {
        datum nk;
        n++;
        nk = gdbm_nextkey(db, key);
        free(key.dptr);
        key = nk;
    }
    printf("iterated keys: %d\n", n);
    failures += CHECK(n == 2);

    /* 5. 杂项 */
    failures += CHECK(gdbm_needs_recovery(db) == 0);
    failures += CHECK(gdbm_fdesc(db) >= 0);
    {
        int cs = 64 * 1024;
        failures += CHECK(gdbm_setopt(db, GDBM_SETCACHESIZE, &cs, (int)sizeof cs) == 0);
    }
    failures += CHECK(gdbm_sync(db) == 0);
    failures += CHECK(gdbm_reorganize(db) == 0);

    /* 6. dump / export / import */
    fp = fopen(dump, "wb");
    failures += CHECK(fp != NULL);
    if (fp) {
        failures += CHECK(gdbm_dump_to_file(db, fp, GDBM_DUMP_FMT_ASCII) == 0);
        fclose(fp);
    }
    fp = fopen(dump, "rb");
    failures += CHECK(fp != NULL);
    if (fp) {
        long sz;
        fseek(fp, 0, SEEK_END);
        sz = ftell(fp);
        fclose(fp);
        printf("dump size: %ld\n", sz);
        failures += CHECK(sz > 0);
    }
    /* gdbm 1.26 的 export/import 成功时返回处理的记录数（>=0），失败 -1 */
    fp = fopen(dump, "wb");
    if (fp) { failures += CHECK(gdbm_export_to_file(db, fp) >= 0); fclose(fp); }
    fp = fopen(dump, "rb");
    if (fp) { failures += CHECK(gdbm_import_from_file(db, fp, GDBM_REPLACE) >= 0); fclose(fp); }

    /* 7. 删 + 关闭 */
    failures += CHECK(gdbm_delete(db, k) == 0);
    failures += CHECK(gdbm_exists(db, k) == 0);
    failures += CHECK(gdbm_close(db) == 0);

    /* 8. 错误路径 */
    bad_db = gdbm_open("/tmp/rtdb_gdbm_missing_xyz.db", 0, GDBM_READER, 0, NULL);
    failures += CHECK(bad_db == NULL);
    printf("open error: %s\n", gdbm_strerror(*gdbm_errno_location()));

    /* 9. 符号探测（47） */
    for (i = 0; i < (int)(sizeof k_syms / sizeof k_syms[0]); i++) {
        if (!gdbm_dyn_has_symbol(k_syms[i])) {
            printf("missing symbol: %s\n", k_syms[i]);
            bad++;
        }
    }
    printf("symbols: %d checked, %d missing\n",
           (int)(sizeof k_syms / sizeof k_syms[0]), bad);
    failures += CHECK(bad == 0);
    failures += CHECK((int)(sizeof k_syms / sizeof k_syms[0]) == 47);

    remove(path);
    remove(dump);
    gdbm_dyn_cleanup();
    if (failures) { printf("%d checks failed\n", failures); return 1; }
    printf("all checks passed\n");
    return 0;
}

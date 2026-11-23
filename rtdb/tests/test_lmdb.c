/* lmdb_dyn 测试：只链 -ldl，不链 liblmdb
 * 覆盖：版本、env 创建/参数/打开、txn、dbi、put/get/del、
 *       cursor、stat、env_info、reader_check、错误码，
 *       以及 56 个导出符号的 has_symbol 探测。
 */
#include "test_common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct MDB_env MDB_env;
typedef struct MDB_txn MDB_txn;
typedef struct MDB_cursor MDB_cursor;
typedef unsigned int MDB_dbi;

typedef struct { size_t mv_size; void *mv_data; } MDB_val;
typedef struct {
    unsigned int ms_psize, ms_depth;
    size_t ms_branch_pages, ms_leaf_pages, ms_overflow_pages, ms_entries;
} MDB_stat;
typedef struct {
    void *me_mapaddr;
    size_t me_mapsize, me_last_pgno, me_last_txnid;
    unsigned int me_maxreaders, me_numreaders;
} MDB_envinfo;

#define MDB_CREATE      0x40000
#define MDB_RDONLY      0x20000
#define MDB_DUPSORT     0x04
#define MDB_FIRST       0
#define MDB_NOTFOUND    (-30798)
#define MDB_INCOMPATIBLE (-30784)

extern int  lmdb_dyn_init(const char *);
extern int  lmdb_dyn_libok(void);
extern void lmdb_dyn_cleanup(void);
extern int  lmdb_dyn_has_symbol(const char *);

extern char *mdb_version(int *, int *, int *);
extern char *mdb_strerror(int);
extern int   mdb_env_create(MDB_env **);
extern int   mdb_env_open(MDB_env *, const char *, unsigned int, mode_t);
extern int   mdb_env_stat(MDB_env *, MDB_stat *);
extern int   mdb_env_info(MDB_env *, MDB_envinfo *);
extern int   mdb_env_sync(MDB_env *, int);
extern void  mdb_env_close(MDB_env *);
extern int   mdb_env_get_path(MDB_env *, const char **);
extern int   mdb_env_get_flags(MDB_env *, unsigned int *);
extern int   mdb_env_set_mapsize(MDB_env *, size_t);
extern int   mdb_env_set_maxdbs(MDB_env *, MDB_dbi);
extern int   mdb_env_get_maxkeysize(MDB_env *);
extern int   mdb_env_set_userctx(MDB_env *, void *);
extern void *mdb_env_get_userctx(MDB_env *);
extern int   mdb_txn_begin(MDB_env *, MDB_txn *, unsigned int, MDB_txn **);
extern int   mdb_txn_commit(MDB_txn *);
extern void  mdb_txn_abort(MDB_txn *);
extern int   mdb_dbi_open(MDB_txn *, const char *, unsigned int, MDB_dbi *);
extern void  mdb_dbi_close(MDB_env *, MDB_dbi);
extern int   mdb_stat(MDB_txn *, MDB_dbi, MDB_stat *);
extern int   mdb_get(MDB_txn *, MDB_dbi, MDB_val *, MDB_val *);
extern int   mdb_put(MDB_txn *, MDB_dbi, MDB_val *, MDB_val *, unsigned int);
extern int   mdb_del(MDB_txn *, MDB_dbi, MDB_val *, MDB_val *);
extern int   mdb_cursor_open(MDB_txn *, MDB_dbi, MDB_cursor **);
extern void  mdb_cursor_close(MDB_cursor *);
extern int   mdb_cursor_get(MDB_cursor *, MDB_val *, MDB_val *, int);
extern int   mdb_cursor_count(MDB_cursor *, size_t *);
extern int   mdb_reader_check(MDB_env *, int *);
extern int   mdb_cmp(MDB_txn *, MDB_dbi, const MDB_val *, const MDB_val *);

static const char *const k_syms[] = {
    "mdb_version", "mdb_strerror", "mdb_env_create", "mdb_env_open", "mdb_env_copy",
    "mdb_env_copyfd", "mdb_env_copy2", "mdb_env_copyfd2", "mdb_env_stat", "mdb_env_info",
    "mdb_env_sync", "mdb_env_close", "mdb_env_set_flags", "mdb_env_get_flags",
    "mdb_env_get_path", "mdb_env_get_fd", "mdb_env_set_mapsize", "mdb_env_set_maxreaders",
    "mdb_env_get_maxreaders", "mdb_env_set_maxdbs", "mdb_env_get_maxkeysize",
    "mdb_env_set_userctx", "mdb_env_get_userctx", "mdb_env_set_assert", "mdb_txn_begin",
    "mdb_txn_env", "mdb_txn_id", "mdb_txn_commit", "mdb_txn_abort", "mdb_txn_reset",
    "mdb_txn_renew", "mdb_dbi_open", "mdb_stat", "mdb_dbi_flags", "mdb_dbi_close",
    "mdb_drop", "mdb_set_compare", "mdb_set_dupsort", "mdb_set_relfunc", "mdb_set_relctx",
    "mdb_get", "mdb_put", "mdb_del", "mdb_cursor_open", "mdb_cursor_close",
    "mdb_cursor_renew", "mdb_cursor_txn", "mdb_cursor_dbi", "mdb_cursor_get",
    "mdb_cursor_put", "mdb_cursor_del", "mdb_cursor_count", "mdb_cmp", "mdb_dcmp",
    "mdb_reader_list", "mdb_reader_check"
};

static MDB_val mv(void *p, size_t n) {
    MDB_val v;
    v.mv_data = p;
    v.mv_size = n;
    return v;
}

int main(int argc, char **argv) {
    int failures = 0, i, bad = 0, rc, dead = 0;
    const char *path = "/tmp/rtdb_lmdb_test";
    char f1[256], f2[256];
    MDB_env *env = NULL;
    MDB_txn *txn = NULL;
    MDB_dbi dbi = 0;
    MDB_dbi dbi2 = 0;
    MDB_cursor *cur = NULL;
    MDB_stat st;
    MDB_envinfo ei;
    MDB_val k, v, got;
    int major = 0, minor = 0, patch = 0;
    char *ver;

    if (lmdb_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("lmdb 不可加载\n");
    failures += CHECK(lmdb_dyn_libok() != 0);

    /* 1. 版本 */
    ver = mdb_version(&major, &minor, &patch);
    printf("lmdb %s (%d.%d.%d)\n", ver ? ver : "?", major, minor, patch);
    failures += CHECK(ver != NULL);

    /* 2. env */
    mkdir(path, 0777);
    failures += CHECK(mdb_env_create(&env) == 0);
    failures += CHECK(env != NULL);
    failures += CHECK(mdb_env_set_maxdbs(env, 4) == 0);
    failures += CHECK(mdb_env_set_mapsize(env, (size_t)16 * 1024 * 1024) == 0);
    failures += CHECK(mdb_env_set_userctx(env, (void *)&failures) == 0);
    failures += CHECK(mdb_env_get_userctx(env) == (void *)&failures);
    rc = mdb_env_open(env, path, MDB_CREATE, 0664);
    if (rc != 0) printf("env_open: %s\n", mdb_strerror(rc));
    failures += CHECK(rc == 0);
    if (rc != 0) { printf("%d checks failed\n", failures); return 1; }

    {
        const char *p = NULL;
        unsigned int flags = 0;
        failures += CHECK(mdb_env_get_path(env, &p) == 0 && p != NULL);
        failures += CHECK(mdb_env_get_flags(env, &flags) == 0);
        printf("path=%s flags=0x%x maxkey=%d\n", p ? p : "?", flags,
               mdb_env_get_maxkeysize(env));
        failures += CHECK(mdb_env_get_maxkeysize(env) > 0);
    }

    /* 3. 写事务 */
    failures += CHECK(mdb_txn_begin(env, NULL, 0, &txn) == 0);
    failures += CHECK(mdb_dbi_open(txn, NULL, 0, &dbi) == 0);
    k = mv("k1", 2); v = mv("v1", 2);
    failures += CHECK(mdb_put(txn, dbi, &k, &v, 0) == 0);
    k = mv("k2", 2); v = mv("v2", 2);
    failures += CHECK(mdb_put(txn, dbi, &k, &v, 0) == 0);

    /* DUPSORT 子库：同一 key 两个值，供 cursor_count 使用 */
    failures += CHECK(mdb_dbi_open(txn, "dups", MDB_CREATE | MDB_DUPSORT, &dbi2) == 0);
    k = mv("dk", 2); v = mv("a", 1);
    failures += CHECK(mdb_put(txn, dbi2, &k, &v, 0) == 0);
    v = mv("b", 1);
    failures += CHECK(mdb_put(txn, dbi2, &k, &v, 0) == 0);

    failures += CHECK(mdb_txn_commit(txn) == 0);

    /* 4. 读事务 */
    failures += CHECK(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) == 0);
    k = mv("k1", 2);
    got = mv(NULL, 0);
    failures += CHECK(mdb_get(txn, dbi, &k, &got) == 0);
    failures += CHECK(got.mv_size == 2 && memcmp(got.mv_data, "v1", 2) == 0);
    k = mv("nope", 4);
    failures += CHECK(mdb_get(txn, dbi, &k, &got) == MDB_NOTFOUND);

    failures += CHECK(mdb_stat(txn, dbi, &st) == 0);
    printf("entries=%zu depth=%u psize=%u\n", st.ms_entries, st.ms_depth, st.ms_psize);
    /* 主库含 2 个键 + 命名子库 "dups" 的元记录 */
    failures += CHECK(st.ms_entries >= 2);

    failures += CHECK(mdb_cursor_open(txn, dbi, &cur) == 0);
    k = mv(NULL, 0); got = mv(NULL, 0);
    failures += CHECK(mdb_cursor_get(cur, &k, &got, MDB_FIRST) == 0);
    /* 非 DUPSORT 库：cursor_count 返回 MDB_INCOMPATIBLE */
    {
        size_t c = 0;
        failures += CHECK(mdb_cursor_count(cur, &c) == MDB_INCOMPATIBLE);
    }
    failures += CHECK(mdb_cmp(txn, dbi, &k, &k) == 0);
    mdb_cursor_close(cur);

    /* DUPSORT 库：同一 key 两个值，cursor_count == 2 */
    failures += CHECK(mdb_cursor_open(txn, dbi2, &cur) == 0);
    k = mv("dk", 2); got = mv(NULL, 0);
    failures += CHECK(mdb_cursor_get(cur, &k, &got, MDB_FIRST) == 0);
    {
        size_t c = 0;
        failures += CHECK(mdb_cursor_count(cur, &c) == 0 && c == 2);
    }
    mdb_cursor_close(cur);
    failures += CHECK(mdb_txn_commit(txn) == 0);

    /* 5. env 统计 / 同步 / reader_check */
    failures += CHECK(mdb_env_info(env, &ei) == 0);
    printf("mapsize=%zu maxreaders=%u\n", ei.me_mapsize, ei.me_maxreaders);
    failures += CHECK(ei.me_mapsize > 0);
    failures += CHECK(mdb_env_stat(env, &st) == 0);
    failures += CHECK(mdb_env_sync(env, 1) == 0);
    failures += CHECK(mdb_reader_check(env, &dead) == 0);

    /* 6. 删除 */
    failures += CHECK(mdb_txn_begin(env, NULL, 0, &txn) == 0);
    k = mv("k1", 2);
    failures += CHECK(mdb_del(txn, dbi, &k, NULL) == 0);
    failures += CHECK(mdb_txn_commit(txn) == 0);
    failures += CHECK(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) == 0);
    k = mv("k1", 2);
    failures += CHECK(mdb_get(txn, dbi, &k, &got) == MDB_NOTFOUND);
    mdb_txn_abort(txn);

    mdb_dbi_close(env, dbi2);
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    /* 7. 符号探测（56） */
    for (i = 0; i < (int)(sizeof k_syms / sizeof k_syms[0]); i++) {
        if (!lmdb_dyn_has_symbol(k_syms[i])) {
            printf("missing symbol: %s\n", k_syms[i]);
            bad++;
        }
    }
    printf("symbols: %d checked, %d missing\n",
           (int)(sizeof k_syms / sizeof k_syms[0]), bad);
    failures += CHECK(bad == 0);
    failures += CHECK((int)(sizeof k_syms / sizeof k_syms[0]) == 56);

    snprintf(f1, sizeof f1, "%s/data.mdb", path);
    snprintf(f2, sizeof f2, "%s/lock.mdb", path);
    remove(f1);
    remove(f2);
    rmdir(path);

    lmdb_dyn_cleanup();
    if (failures) { printf("%d checks failed\n", failures); return 1; }
    printf("all checks passed\n");
    return 0;
}

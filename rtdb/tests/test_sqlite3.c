/* sqlite3_dyn 测试：只链 -ldl，不链 libsqlite3 */
#include "test_common.h"

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_DONE 101
#define SQLITE_INTEGER 1
#define SQLITE_TEXT 5

int sqlite3_dyn_init(const char *library_path);
void sqlite3_dyn_cleanup(void);
int sqlite3_libok(void);

int sqlite3_open(const char *filename, sqlite3 **ppDb);
int sqlite3_close(sqlite3 *db);
int sqlite3_exec(sqlite3 *db, const char *sql,
                 void (*cb)(void *, int, char **, char **), void *ud,
                 char **errmsg);
int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte,
                       sqlite3_stmt **ppStmt, const char **pzTail);
int sqlite3_step(sqlite3_stmt *pStmt);
int sqlite3_finalize(sqlite3_stmt *pStmt);
int sqlite3_column_count(sqlite3_stmt *pStmt);
int sqlite3_column_int(sqlite3_stmt *pStmt, int iCol);
double sqlite3_column_double(sqlite3_stmt *pStmt, int iCol);
int sqlite3_column_type(sqlite3_stmt *pStmt, int iCol);
const char *sqlite3_column_text(sqlite3_stmt *pStmt, int iCol);
const char *sqlite3_column_name(sqlite3_stmt *pStmt, int iCol);
const char *sqlite3_errmsg(sqlite3 *db);
int sqlite3_bind_int(sqlite3_stmt *pStmt, int idx, int val);
int sqlite3_bind_text(sqlite3_stmt *pStmt, int idx, const char *val,
                      int n, void *destructor);

int main(int argc, char **argv) {
    int failures = 0;
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;

    if (sqlite3_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "sqlite3_dyn_init failed\n");
        return 1;
    }
    failures += CHECK(sqlite3_libok() != 0);

    failures += CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);

    failures += CHECK(sqlite3_exec(db,
        "CREATE TABLE t(a INTEGER, b TEXT);"
        "INSERT INTO t VALUES (7,'seven');"
        "INSERT INTO t VALUES (13,'thirteen');", NULL, NULL, NULL)
        == SQLITE_OK);

    failures += CHECK(sqlite3_prepare_v2(db, "SELECT a,b FROM t ORDER BY a",
                                         -1, &st, NULL) == SQLITE_OK);
    failures += CHECK(sqlite3_column_count(st) == 2);
    failures += CHECK(strcmp(sqlite3_column_name(st, 0), "a") == 0);

    failures += CHECK(sqlite3_step(st) == SQLITE_ROW);
    failures += CHECK(sqlite3_column_type(st, 0) == SQLITE_INTEGER);
    failures += CHECK(sqlite3_column_int(st, 0) == 7);
    failures += CHECK(strcmp(sqlite3_column_text(st, 1), "seven") == 0);
    failures += CHECK(sqlite3_column_double(st, 0) == 7.0);

    failures += CHECK(sqlite3_step(st) == SQLITE_ROW);
    failures += CHECK(sqlite3_column_int(st, 0) == 13);

    failures += CHECK(sqlite3_step(st) == SQLITE_DONE);
    failures += CHECK(sqlite3_finalize(st) == SQLITE_OK);

    /* bind 往返 */
    {
        sqlite3_stmt *b = NULL;
        failures += CHECK(sqlite3_prepare_v2(db, "SELECT a FROM t WHERE b=?",
                                             -1, &b, NULL) == SQLITE_OK);
        failures += CHECK(sqlite3_bind_text(b, 1, "thirteen", -1, NULL)
                          == SQLITE_OK);
        failures += CHECK(sqlite3_step(b) == SQLITE_ROW);
        failures += CHECK(sqlite3_column_int(b, 0) == 13);
        failures += CHECK(sqlite3_finalize(b) == SQLITE_OK);
    }

    /* 错误路径：错误 SQL，errmsg 传 NULL（不引入泄漏问题） */
    {
        failures += CHECK(sqlite3_exec(db, "BOGUS SQL", NULL, NULL, NULL)
                          != SQLITE_OK);
        printf("bogus sql errmsg=%s\n", sqlite3_errmsg(db));
    }

    failures += CHECK(sqlite3_close(db) == SQLITE_OK);
    sqlite3_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}

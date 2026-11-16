/* pgsql_dyn 测试：只链 -ldl，不链 libpq
 * 本机无 PostgreSQL 服务时打印 SKIP 退出 0（run-all 不受影响）
 */
#include "test_common.h"

typedef struct PGconn PGconn;
typedef struct PGresult PGresult;
typedef int ConnStatusType;   /* CONNECTION_OK=0 */
typedef int ExecStatusType;   /* PGRES_TUPLES_OK=2 */

int pgsql_dyn_init(const char *library_path);
void pgsql_dyn_cleanup(void);
int pgsql_libok(void);

int PQlibVersion(void);
PGconn *PQconnectdb(const char *conninfo);
ConnStatusType PQstatus(const PGconn *conn);
const char *PQerrorMessage(const PGconn *conn);
PGresult *PQexec(PGconn *conn, const char *query);
ExecStatusType PQresultStatus(const PGresult *res);
int PQntuples(const PGresult *res);
int PQnfields(const PGresult *res);
const char *PQgetvalue(const PGresult *res, int tup_num, int field_num);
void PQclear(PGresult *res);
void PQfinish(PGconn *conn);

int main(int argc, char **argv) {
    int failures = 0;
    PGconn *c;
    PGresult *r;

    if (pgsql_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "pgsql_dyn_init failed\n");
        return 1;
    }
    failures += CHECK(pgsql_libok() != 0);

    failures += CHECK(PQlibVersion() > 0);
    printf("libpq version=%d (0x%x)\n", PQlibVersion(), PQlibVersion());

    c = PQconnectdb(
        "host=localhost port=5432 dbname=postgres connect_timeout=2");
    failures += CHECK(c != NULL);

    if (PQstatus(c) != 0 /* CONNECTION_OK */) {
        printf("SKIP: no local postgres server (%s)\n", PQerrorMessage(c));
        if (c) PQfinish(c);
        pgsql_dyn_cleanup();
        return 0;
    }

    r = PQexec(c, "SELECT 40 + 2");
    failures += CHECK(r != NULL && PQresultStatus(r) == 2 /* TUPLES_OK */);
    failures += CHECK(PQntuples(r) == 1 && PQnfields(r) == 1);
    failures += CHECK(strcmp(PQgetvalue(r, 0, 0), "42") == 0);
    printf("result=%s\n", PQgetvalue(r, 0, 0));
    PQclear(r);

    PQfinish(c);
    pgsql_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}

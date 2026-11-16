/* mysql_dyn 测试：只链 -ldl，不链 libmysqlclient
 * 本机无 MySQL 服务时打印 SKIP 退出 0（run-all 不受影响）
 */
#include "test_common.h"

typedef struct MYSQL MYSQL;
typedef struct MYSQL_RES MYSQL_RES;
typedef char **MYSQL_ROW;

int mysql_dyn_init(const char *library_path);
void mysql_dyn_cleanup(void);
int mysql_libok(void);

unsigned long mysql_get_client_version(void);
MYSQL *mysql_init(MYSQL *mysql);
MYSQL *mysql_real_connect(MYSQL *mysql, const char *host, const char *user,
                          const char *passwd, const char *db,
                          unsigned int port, const char *unix_socket,
                          unsigned long clientflag);
const char *mysql_error(MYSQL *mysql);
unsigned int mysql_errno(MYSQL *mysql);
int mysql_query(MYSQL *mysql, const char *query);
MYSQL_RES *mysql_store_result(MYSQL *mysql);
MYSQL_ROW mysql_fetch_row(MYSQL_RES *result);
unsigned int mysql_num_fields(MYSQL_RES *result);
void mysql_free_result(MYSQL_RES *result);
void mysql_close(MYSQL *mysql);

int main(int argc, char **argv) {
    int failures = 0;
    MYSQL *m;
    MYSQL_RES *res;
    MYSQL_ROW row;

    if (mysql_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        printf("SKIP: libmysqlclient not available on this system\n");
        mysql_dyn_cleanup();
        return 0;
    }
    failures += CHECK(mysql_libok() != 0);

    failures += CHECK(mysql_get_client_version() > 0);
    printf("client version=%lu\n", mysql_get_client_version());

    m = mysql_init(NULL);
    failures += CHECK(m != NULL);

    if (!mysql_real_connect(m, "localhost", "root", "", NULL, 0, NULL, 0)) {
        printf("SKIP: no local mysql server (errno=%u: %s)\n",
               mysql_errno(m), mysql_error(m));
        if (m) mysql_close(m);
        mysql_dyn_cleanup();
        return 0;
    }

    failures += CHECK(mysql_query(m, "SELECT 1+1") == 0);
    res = mysql_store_result(m);
    failures += CHECK(res != NULL);
    if (res) {
        failures += CHECK(mysql_num_fields(res) == 1);
        row = mysql_fetch_row(res);
        failures += CHECK(row && row[0] && strcmp(row[0], "2") == 0);
        mysql_free_result(res);
    }

    mysql_close(m);
    mysql_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}

/* bdb_dyn 测试：只链 -ldl，不链 libdb
 * 注意：封装层只覆盖 db_create/db_strerror/db_version 等入口；
 * DB 方法式调用（dbp->open）需要 DB 结构布局，不在本测试范围。
 */
#include "test_common.h"
#include <stdint.h>

typedef struct __db DB;
typedef struct __db_env DB_ENV;
typedef uint32_t u_int32_t;

int db_dyn_init(const char *library_path);
void db_dyn_cleanup(void);
int db_libok(void);
int db_dyn_loaded_version(void);

int db_create(DB **dbp, DB_ENV *dbenv, u_int32_t flags);
char *db_strerror(int error);
char *db_version(int *majorp, int *minorp, int *patchp);

int main(int argc, char **argv) {
    int failures = 0;
    int ma = 0, mi = 0, pa = 0;
    DB *db = NULL;

    if (db_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "db_dyn_init failed\n");
        return 1;
    }
    failures += CHECK(db_libok() != 0);

    db_version(&ma, &mi, &pa);
    printf("bdb version=%d.%d.%d (loaded=%d)\n", ma, mi, pa,
           db_dyn_loaded_version());
    failures += CHECK(ma >= 4);
    failures += CHECK(db_dyn_loaded_version() == ma);

    /* db_create：成功返回 0，指针非空 */
    failures += CHECK(db_create(&db, NULL, 0) == 0);
    failures += CHECK(db != NULL);

    /* db_strerror：0 -> 成功信息（各版本带 BDB 前缀），负值 -> 错误信息 */
    failures += CHECK(db_strerror(0) != NULL &&
                      strstr(db_strerror(0), "Successful return: 0") != NULL);
    failures += CHECK(db_strerror(-30999) != NULL);

    db_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}

/* python_dyn 测试：只链 -ldl，不链 libpython */
#include "test_common.h"

int python_dyn_init(const char *library_path);
void python_dyn_cleanup(void);
int python_dyn_major(void);
int python_dyn_minor(void);

void Py_Initialize(void);
int Py_IsInitialized(void);
int PyRun_SimpleString(const char *command);
void Py_Finalize(void);
const char *Py_GetVersion(void);

int main(int argc, char **argv) {
    int failures = 0;

    /* python_dyn_init 内部做 3.x / 2.7 版本探测 */
    if (python_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "python_dyn_init failed\n");
        return 1;
    }
    printf("loaded libpython %d.%d (%s)\n", python_dyn_major(),
           python_dyn_minor(), Py_GetVersion());

    failures += CHECK(python_dyn_major() == 3 || python_dyn_major() == 2);
    failures += CHECK(Py_GetVersion() != NULL && Py_GetVersion()[0] >= '0'
                      && Py_GetVersion()[0] <= '9');

    failures += CHECK(Py_IsInitialized() == 0);
    Py_Initialize();
    failures += CHECK(Py_IsInitialized() != 0);

    failures += CHECK(PyRun_SimpleString("print('py ok')") == 0);

    /* 运算 + 变量跨语句生效 */
    failures += CHECK(PyRun_SimpleString("x = 20 + 22") == 0);
    failures += CHECK(PyRun_SimpleString("print('x =', x)") == 0);

    /* 错误路径：语法错误返回 -1 */
    failures += CHECK(PyRun_SimpleString("def (") == -1);

    Py_Finalize();
    python_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}

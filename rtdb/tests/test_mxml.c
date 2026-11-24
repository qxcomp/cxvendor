/* mxml_dyn 测试：只链 -ldl，不链 libmxml
 * 覆盖：动态加载、v3/v4 自动探测、控制函数、符号探测，
 *       以及有库时的 create -> save -> load round-trip。
 * 本机无 libmxml 时按 TEST_SKIP 正常退出。
 */
#include "test_common.h"
#include <stdio.h>

typedef struct _mxml_node_s mxml_node_t;
typedef struct _mxml_options_s mxml_options_t;
typedef enum { MXML_T_IGNORE = -1, MXML_T_ELEMENT = 4, MXML_T_TEXT = 8 } mxml_test_type_t;

extern int  mxml_dyn_init(const char *);
extern int  mxml_dyn_libok(void);
extern void mxml_dyn_cleanup(void);
extern int  mxml_dyn_has_symbol(const char *);
extern int  mxml_dyn_abi(void);

extern mxml_node_t *mxmlNewXML(const char *);
extern mxml_node_t *mxmlNewElement(mxml_node_t *, const char *);
extern mxml_node_t *mxmlNewText(mxml_node_t *, int, const char *);
extern void          mxmlDelete(mxml_node_t *);
extern char         *mxmlSaveAllocString(mxml_node_t *, mxml_options_t *);
extern mxml_node_t  *mxmlLoadString(mxml_node_t *, mxml_options_t *, const char *);
extern mxml_options_t *mxmlOptionsNew(void);
extern void          mxmlOptionsDelete(mxml_options_t *);
extern const char   *mxmlGetElement(const mxml_node_t *);
extern mxml_node_t  *mxmlGetFirstChild(mxml_node_t *);
extern int           mxmlGetType(mxml_node_t *);

int main(void) {
    int abi, errs = 0;
    mxml_node_t *root, *child, *loaded, *lc;
    mxml_options_t *opts = NULL;
    char *xml;

    if (mxml_dyn_init(NULL) != 0)
        TEST_SKIP("libmxml not installed, skipping mxml_dyn test\n");

    errs += CHECK(mxml_dyn_libok());
    abi = mxml_dyn_abi();
    printf("info: detected mxml ABI = %d\n", abi);
    errs += CHECK(abi == 3 || abi == 4 || abi == -1);

    errs += CHECK(mxml_dyn_has_symbol("mxmlNewXML"));
    errs += CHECK(mxml_dyn_has_symbol("mxmlAdd"));
    errs += CHECK(mxml_dyn_has_symbol("mxmlLoadFile"));
    if (abi == 4) errs += CHECK(mxml_dyn_has_symbol("mxmlOptionsNew"));
    if (abi == 3) errs += CHECK(mxml_dyn_has_symbol("mxmlSetWrapMargin"));

    root = mxmlNewXML("1.0");
    errs += CHECK(root != NULL);
    if (!root) return 1;
    child = mxmlNewElement(root, "hello");
    errs += CHECK(child != NULL);
    errs += CHECK(mxmlNewText(child, 0, "world") != NULL);

    if (abi == 4) {
        opts = mxmlOptionsNew();
        errs += CHECK(opts != NULL);
    }
    xml = mxmlSaveAllocString(root, opts);
    errs += CHECK(xml != NULL);
    if (xml) {
        printf("info: saved xml = %s\n", xml);
        errs += CHECK(strstr(xml, "hello") != NULL);
        loaded = mxmlLoadString(NULL, opts, xml);
        errs += CHECK(loaded != NULL);
        if (loaded) {
            lc = mxmlGetFirstChild(loaded);
            errs += CHECK(lc != NULL);
            if (lc) {
                errs += CHECK(strcmp(mxmlGetElement(lc), "hello") == 0);
                printf("info: loaded element type = %d\n", mxmlGetType(lc));
            }
            mxmlDelete(loaded);
        }
    }

    if (opts) mxmlOptionsDelete(opts);
    mxmlDelete(root);
    mxml_dyn_cleanup();
    if (errs) { fprintf(stderr, "mxml_dyn: %d failure(s)\n", errs); return 1; }
    printf("mxml_dyn: all checks passed\n");
    return 0;
}

/* tinyxml2_dyn 测试：只链 -ldl，不链 libtinyxml2
 * tinyxml2 是纯 C++ 类库，封装层以 mangled 名 dlopen，this 用 void*。
 * 覆盖: selftest、解析、属性/整数、子节点、序列化、错误路径、
 *       XMLPrinter 构造/析构（vtable slot0）、代表性符号探测。
 */
#include "test_common.h"
#include <stddef.h>

/* ------------------------- 控制函数 ------------------------- */
extern int tinyxml2_dyn_init(const char *);
extern int tinyxml2_libok(void);
extern void tinyxml2_dyn_cleanup(void);
extern int tinyxml2_dyn_has_symbol(const char *);
extern int tinyxml2_dyn_selftest(void);

/* ------------------------- 便捷层 ------------------------- */
extern void *tinyxml2_dyn_document_new(void);
extern void tinyxml2_dyn_document_free(void *);
extern void *tinyxml2_dyn_printer_new(void);
extern void tinyxml2_dyn_printer_free(void *);

/* ------------------------- 被封装函数声明 ------------------------- */
extern int tinyxml2_XMLDocument_Parse_mem(void *, const char *, size_t);
extern const char *tinyxml2_XMLDocument_ErrorStr(const void *);
extern const char *tinyxml2_XMLDocument_ErrorName(const void *);
extern const char *tinyxml2_XMLDocument_ErrorIDToName(int);
extern int tinyxml2_XMLDocument_SaveFile(void *, const char *, unsigned char);
extern void tinyxml2_XMLDocument_Print(const void *, void *);

extern void *tinyxml2_XMLNode_FirstChildElement(const void *, const char *);
extern const char *tinyxml2_XMLNode_Value(const void *);
extern int tinyxml2_XMLNode_ChildElementCount(const void *);

extern const char *tinyxml2_XMLElement_GetText(const void *);
extern const char *tinyxml2_XMLElement_Attribute(const void *, const char *, const char *);
extern int tinyxml2_XMLElement_IntAttribute(const void *, const char *, int);
extern unsigned char tinyxml2_XMLElement_QueryIntText(const void *, int *);
extern void *tinyxml2_XMLElement_InsertNewChildElement(void *, const char *);
extern void tinyxml2_XMLElement_SetText_int(void *, int);
extern void tinyxml2_XMLElement_SetText_cstr(void *, const char *);

/* ------------------------- 代表性 mangled 符号 ------------------------- */
static const char *const k_syms[] = {
    "_ZN8tinyxml211XMLDocumentC1EbNS_10WhitespaceE",
    "_ZN8tinyxml211XMLDocumentD1Ev",
    "_ZN8tinyxml211XMLDocument5ParseEPKcm",
    "_ZN8tinyxml211XMLDocument8SaveFileEPKcb",
    "_ZN8tinyxml211XMLDocument13ErrorIDToNameENS_8XMLErrorE",
    "_ZNK8tinyxml211XMLDocument8ErrorStrEv",
    "_ZN8tinyxml211XMLDocument10NewElementEPKc",
    "_ZN8tinyxml211XMLDocument10NewCommentEPKc",
    "_ZNK8tinyxml27XMLNode17FirstChildElementEPKc",
    "_ZNK8tinyxml27XMLNode5ValueEv",
    "_ZN8tinyxml27XMLNode14InsertEndChildEPS0_",
    "_ZN8tinyxml27XMLNode14DeleteChildrenEv",
    "_ZN8tinyxml210XMLElement21InsertNewChildElementEPKc",
    "_ZN8tinyxml210XMLElement7SetTextEi",
    "_ZN8tinyxml210XMLElement7SetTextEPKc",
    "_ZNK8tinyxml210XMLElement7GetTextEv",
    "_ZNK8tinyxml210XMLElement9AttributeEPKcS2_",
    "_ZNK8tinyxml210XMLElement12IntAttributeEPKci",
    "_ZNK8tinyxml212XMLAttribute4NameEv",
    "_ZNK8tinyxml212XMLAttribute5ValueEv",
    "_ZN8tinyxml210XMLPrinterC1EP8_IO_FILEbi",
    "_ZN8tinyxml210XMLPrinter5PrintEPKcz",
    "_ZN8tinyxml210XMLPrinter5WriteEPKcm",
    "_ZN8tinyxml27XMLUtil5ToStrEiPci",
    "_ZN8tinyxml27XMLUtil5ToIntEPKcPi",
    "_ZN8tinyxml27XMLUtil6ToBoolEPKcPb",
    "_ZN8tinyxml27XMLUtil8ToDoubleEPKcPd",
    "_ZN8tinyxml210XMLCommentC1EPNS_11XMLDocumentE",
    "_ZN8tinyxml27XMLText9ParseDeepEPcPNS_7StrPairEPi",
    "_ZN8tinyxml27StrPair6GetStrEv"
};

int main(int argc, char **argv) {
    int failures = 0;
    int i, bad = 0;
    void *doc;

    if (tinyxml2_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("tinyxml2 不可加载\n");
    failures += CHECK(tinyxml2_libok() != 0);

    /* 1. 内建自检 */
    failures += CHECK(tinyxml2_dyn_selftest() == 0);

    /* 2. 解析 + 树遍历 + 属性 */
    doc = tinyxml2_dyn_document_new();
    failures += CHECK(doc != NULL);
    failures += CHECK(tinyxml2_XMLDocument_Parse_mem(doc, "<root a='7'><c>x</c></root>", strlen("<root a='7'><c>x</c></root>")) == 0);
    {
        void *root = tinyxml2_XMLNode_FirstChildElement(doc, "root");
        failures += CHECK(root != NULL);
        if (root) {
            const char *v = tinyxml2_XMLNode_Value(root);
            printf("root value: %s\n", v ? v : "(null)");
            failures += CHECK(v != NULL && strcmp(v, "root") == 0);
            printf("a attr: %s / int %d\n",
                   tinyxml2_XMLElement_Attribute(root, "a", NULL),
                   tinyxml2_XMLElement_IntAttribute(root, "a", -1));
            failures += CHECK(tinyxml2_XMLElement_Attribute(root, "a", NULL) != NULL);
            failures += CHECK(strcmp(tinyxml2_XMLElement_Attribute(root, "a", NULL), "7") == 0);
            /* 第二参是过滤值：匹配返回指针，不匹配返回 NULL */
            failures += CHECK(tinyxml2_XMLElement_Attribute(root, "a", "7") != NULL);
            failures += CHECK(tinyxml2_XMLElement_Attribute(root, "a", "x") == NULL);
            failures += CHECK(tinyxml2_XMLElement_IntAttribute(root, "a", -1) == 7);
            failures += CHECK(tinyxml2_XMLNode_ChildElementCount(root) == 1);
        }
    }

    /* 3. 子元素文本 + QueryIntText */
    {
        void *root = tinyxml2_XMLNode_FirstChildElement(doc, "root");
        void *c = root ? tinyxml2_XMLNode_FirstChildElement(root, "c") : NULL;
        failures += CHECK(c != NULL);
        if (c) {
            const char *t = tinyxml2_XMLElement_GetText(c);
            printf("c text: %s\n", t ? t : "(null)");
            failures += CHECK(t != NULL && strcmp(t, "x") == 0);
        }
    }

    /* 4. 插入子节点 + SetText */
    {
        void *root = tinyxml2_XMLNode_FirstChildElement(doc, "root");
        void *d = root ? tinyxml2_XMLElement_InsertNewChildElement(root, "d") : NULL;
        failures += CHECK(d != NULL);
        if (d) tinyxml2_XMLElement_SetText_int(d, 5);
        if (root) failures += CHECK(tinyxml2_XMLNode_ChildElementCount(root) == 2);
    }

    /* 5. 序列化到文件 */
    {
        const char *path = "/tmp/tinyxml2_dyn_test.xml";
        FILE *fp;
        char buf[512];
        size_t n = 0;
        failures += CHECK(tinyxml2_XMLDocument_SaveFile(doc, path, 0) == 0);
        fp = fopen(path, "rb");
        failures += CHECK(fp != NULL);
        if (fp) { n = fread(buf, 1, sizeof buf - 1, fp); fclose(fp); }
        buf[n] = 0;
        printf("saved: %s", buf);
        failures += CHECK(strstr(buf, "<d>5</d>") != NULL);
        remove(path);
    }

    /* 6. XMLPrinter（内存模式）+ 析构（vtable slot0） */
    {
        void *pr = tinyxml2_dyn_printer_new();
        failures += CHECK(pr != NULL);
        if (pr) {
            tinyxml2_XMLDocument_Print(doc, pr);
            tinyxml2_dyn_printer_free(pr);
        }
    }

    tinyxml2_dyn_document_free(doc);

    /* 7. 错误路径 */
    {
        void *bad_doc = tinyxml2_dyn_document_new();
        int err;
        failures += CHECK(bad_doc != NULL);
        err = tinyxml2_XMLDocument_Parse_mem(bad_doc, "<a><b></a>", strlen("<a><b></a>"));
        failures += CHECK(err != 0);
        printf("parse error: %s (%s)\n",
               tinyxml2_XMLDocument_ErrorName(bad_doc),
               tinyxml2_XMLDocument_ErrorStr(bad_doc));
        failures += CHECK(tinyxml2_XMLDocument_ErrorName(bad_doc) != NULL);
        failures += CHECK(tinyxml2_XMLDocument_ErrorStr(bad_doc) != NULL);
        failures += CHECK(tinyxml2_XMLDocument_ErrorIDToName(err) != NULL);
        tinyxml2_dyn_document_free(bad_doc);
    }

    /* 8. 代表性符号探测 */
    for (i = 0; i < (int)(sizeof k_syms / sizeof k_syms[0]); i++) {
        if (!tinyxml2_dyn_has_symbol(k_syms[i])) {
            printf("missing symbol: %s\n", k_syms[i]);
            bad++;
        }
    }
    printf("symbols: %d checked, %d missing\n",
           (int)(sizeof k_syms / sizeof k_syms[0]), bad);
    failures += CHECK(bad == 0);

    tinyxml2_dyn_cleanup();
    if (failures) { printf("%d checks failed\n", failures); return 1; }
    printf("all checks passed\n");
    return 0;
}

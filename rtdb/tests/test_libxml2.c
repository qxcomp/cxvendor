/* libxml2_dyn 测试：只链 -ldl，不链 libxml2
 * 覆盖：版本与数据符号、内存解析、树遍历、XPath、文档构建与序列化、
 *       正则、缓冲区、xmlStrPrintf 变参路径，以及代表性符号探测。
 * 只用不透明指针，不依赖 libxml2 结构体布局。
 */
#include "test_common.h"
#include <stddef.h>

typedef unsigned char xmlChar;
typedef void *xmlDocPtr;
typedef void *xmlNodePtr;
typedef void *xmlXPathContextPtr;
typedef void *xmlXPathObjectPtr;
typedef void *xmlRegexpPtr;
typedef void *xmlBufferPtr;
typedef void *xmlErrorPtr;

/* ------------------------- 控制函数 ------------------------- */
extern int libxml2_dyn_init(const char *);
extern int libxml2_libok(void);
extern void libxml2_dyn_cleanup(void);
extern int libxml2_dyn_has_symbol(const char *);
extern void libxml2_dyn_sync_globals(void);

/* ------------------------- 数据符号 ------------------------- */
extern const char *xmlParserVersion;
extern void (*xmlFree)(void *);
extern xmlChar xmlStringText[];

/* ------------------------- 被封装函数声明 ------------------------- */
extern void xmlInitParser(void);
extern void xmlCleanupParser(void);
extern xmlDocPtr xmlReadMemory(const char *, int, const char *, const char *, int);
extern xmlNodePtr xmlDocGetRootElement(const void *);
extern xmlNodePtr xmlFirstElementChild(const void *);
extern xmlChar *xmlNodeGetContent(const void *);
extern xmlErrorPtr xmlGetLastError(void);
extern void xmlResetLastError(void);
extern void xmlFreeDoc(xmlDocPtr);
extern int xmlStrPrintf(xmlChar *, int, const char *, ...);

extern xmlDocPtr xmlNewDoc(const xmlChar *);
extern xmlNodePtr xmlNewNode(void *, const xmlChar *);
extern xmlDocPtr xmlDocSetRootElement(xmlDocPtr, xmlNodePtr);
extern void xmlDocDumpMemory(xmlDocPtr, xmlChar **, int *);

extern xmlXPathContextPtr xmlXPathNewContext(xmlDocPtr);
extern void xmlXPathFreeContext(xmlXPathContextPtr);
extern xmlXPathObjectPtr xmlXPathEvalExpression(const xmlChar *, xmlXPathContextPtr);
extern void xmlXPathFreeObject(xmlXPathObjectPtr);
extern xmlChar *xmlXPathCastToString(xmlXPathObjectPtr);

extern xmlRegexpPtr xmlRegexpCompile(const xmlChar *);
extern int xmlRegexpExec(xmlRegexpPtr, const xmlChar *);
extern void xmlRegFreeRegexp(xmlRegexpPtr);

extern xmlBufferPtr xmlBufferCreate(void);
extern int xmlBufferAdd(xmlBufferPtr, const xmlChar *, int);
extern const xmlChar *xmlBufferContent(const void *);
extern void xmlBufferFree(xmlBufferPtr);
/* ------------------------- 代表性符号 ------------------------- */
static const char *const k_syms[] = {
    "xmlReadMemory", "xmlReadFile", "xmlReadDoc", "xmlRecoverMemory",
    "xmlParseMemory", "xmlParseDoc", "xmlNewDoc", "xmlFreeDoc",
    "xmlDocGetRootElement", "xmlDocSetRootElement", "xmlNewNode", "xmlFreeNode",
    "xmlNodeGetContent", "xmlNodeSetContent", "xmlFirstElementChild",
    "xmlNextElementSibling", "xmlAddChild", "xmlUnlinkNode",
    "xmlGetLastError", "xmlResetLastError", "xmlStrdup", "xmlStrlen",
    "xmlStrPrintf", "xmlStrVPrintf", "xmlStrcmp", "xmlStrEqual",
    "xmlNewProp", "xmlSetProp", "xmlGetProp", "xmlHasProp",
    "xmlXPathNewContext", "xmlXPathFreeContext", "xmlXPathEvalExpression",
    "xmlXPathFreeObject", "xmlXPathCastToString", "xmlXPathCompile",
    "xmlRegexpCompile", "xmlRegexpExec", "xmlRegFreeRegexp",
    "xmlBufferCreate", "xmlBufferFree", "xmlBufferAdd", "xmlBufferContent",
    "xmlNewTextWriter", "xmlFreeTextWriter", "xmlTextWriterStartElement",
    "xmlTextWriterEndElement", "xmlReaderForMemory", "xmlTextReaderRead",
    "xmlFreeTextReader", "xmlSchemaNewParserCtxt", "xmlSchemaFreeParserCtxt",
    "xmlRelaxNGNewParserCtxt", "xmlRelaxNGFreeParserCtxt",
    "xmlHashCreate", "xmlHashFree", "xmlDictCreate", "xmlDictFree",
    "xmlParseURI", "xmlFreeURI", "xmlNewCatalog", "xmlFreeCatalog",
    "htmlReadMemory", "htmlDocDumpMemory", "xmlSaveFormatFile",
    "xmlC14NDocDumpMemory", "xmlInitParser", "xmlCleanupParser"
};

int main(int argc, char **argv) {
    int failures = 0;
    int i, bad = 0;
    xmlDocPtr doc;
    xmlNodePtr root;

    if (libxml2_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("libxml2 不可加载\n");
    failures += CHECK(libxml2_libok() != 0);

    /* 1. 数据符号 */
    printf("libxml2 %s\n", xmlParserVersion ? xmlParserVersion : "(null)");
    failures += CHECK(xmlParserVersion != NULL);
    failures += CHECK(xmlFree != NULL);
    failures += CHECK(xmlStringText[0] == 't');   /* "text" */

    xmlInitParser();

    /* 2. 内存解析 + 树遍历 */
    doc = xmlReadMemory("<root><a k='1'>hi</a></root>", (int)strlen("<root><a k='1'>hi</a></root>"), "mem.xml", NULL, 0);
    failures += CHECK(doc != NULL);
    if (doc) {
        root = xmlDocGetRootElement(doc);
        failures += CHECK(root != NULL);
        {
            xmlNodePtr a = xmlFirstElementChild(root);
            xmlChar *content = a ? xmlNodeGetContent(a) : NULL;
            printf("root/a content: '%s'\n", content ? (const char *)content : "(null)");
            failures += CHECK(content != NULL && strcmp((const char *)content, "hi") == 0);
            if (content) xmlFree(content);
        }
        /* 3. XPath */
        {
            xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
            xmlXPathObjectPtr obj;
            failures += CHECK(ctx != NULL);
            obj = xmlXPathEvalExpression((const xmlChar *)"//a", ctx);
            failures += CHECK(obj != NULL);
            if (obj) {
                xmlChar *s = xmlXPathCastToString(obj);
                printf("xpath //a -> '%s'\n", s ? (const char *)s : "(null)");
                failures += CHECK(s != NULL && strcmp((const char *)s, "hi") == 0);
                if (s) xmlFree(s);
                xmlXPathFreeObject(obj);
            }
            xmlXPathFreeContext(ctx);
        }
        xmlFreeDoc(doc);
    }

    /* 4. 坏 XML -> NULL + last error */
    doc = xmlReadMemory("<a><b></a>", (int)strlen("<a><b></a>"), "bad.xml", NULL, 0);
    failures += CHECK(doc == NULL);
    failures += CHECK(xmlGetLastError() != NULL);
    if (doc) xmlFreeDoc(doc);
    xmlResetLastError();

    /* 5. xmlStrPrintf 变参路径 */
    {
        xmlChar buf[64];
        int n = xmlStrPrintf(buf, (int)sizeof buf, "%s-%d", "abc", 42);
        printf("xmlStrPrintf -> '%s' (n=%d)\n", (const char *)buf, n);
        failures += CHECK(strcmp((const char *)buf, "abc-42") == 0);
    }

    /* 6. 文档构建 + 序列化 */
    {
        xmlDocPtr nd = xmlNewDoc((const xmlChar *)"1.0");
        xmlNodePtr nr = xmlNewNode(NULL, (const xmlChar *)"r");
        xmlChar *mem = NULL;
        int len = 0;
        failures += CHECK(nd != NULL && nr != NULL);
        if (nd && nr) {
            xmlDocSetRootElement(nd, nr);
            xmlDocDumpMemory(nd, &mem, &len);
            printf("dump: %d bytes\n", len);
            failures += CHECK(mem != NULL && len > 0);
            if (mem) printf("dump body: %s\n", (const char *)mem);
            if (mem) xmlFree(mem);
        }
        if (nd) xmlFreeDoc(nd);
    }

    /* 7. 正则 */
    {
        xmlRegexpPtr re = xmlRegexpCompile((const xmlChar *)"a+b");
        failures += CHECK(re != NULL);
        if (re) {
            failures += CHECK(xmlRegexpExec(re, (const xmlChar *)"aaab") == 1);
            failures += CHECK(xmlRegexpExec(re, (const xmlChar *)"x") == 0);
            xmlRegFreeRegexp(re);
        }
    }

    /* 8. 缓冲区 */
    {
        xmlBufferPtr b = xmlBufferCreate();
        failures += CHECK(b != NULL);
        if (b) {
            failures += CHECK(xmlBufferAdd(b, (const xmlChar *)"hello", 5) == 0);
            printf("buffer: '%s'\n", (const char *)xmlBufferContent(b));
            failures += CHECK(strcmp((const char *)xmlBufferContent(b), "hello") == 0);
            xmlBufferFree(b);
        }
    }

    /* 9. 代表性符号探测 */
    for (i = 0; i < (int)(sizeof k_syms / sizeof k_syms[0]); i++) {
        if (!libxml2_dyn_has_symbol(k_syms[i])) {
            printf("missing symbol: %s\n", k_syms[i]);
            bad++;
        }
    }
    printf("symbols: %d checked, %d missing\n",
           (int)(sizeof k_syms / sizeof k_syms[0]), bad);
    failures += CHECK(bad == 0);

    xmlCleanupParser();
    libxml2_dyn_cleanup();
    if (failures) { printf("%d checks failed\n", failures); return 1; }
    printf("all checks passed\n");
    return 0;
}

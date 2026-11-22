/* expat_dyn 测试：只链 -ldl，不链 libexpat
 * 覆盖：版本、回调解析、错误码、解析状态、ParserReset、命名空间、
 *       FeatureList，以及 nm -D 全部 83 个导出符号的 has_symbol 探测。
 */
#include "test_common.h"
#include <stddef.h>

/* ------------------------- 类型（与 expat_dyn.c 一致） ------------------------- */
typedef void *XML_Parser;
typedef char XML_Char;
typedef char XML_LChar;
typedef unsigned char XML_Bool;
typedef unsigned long XML_Size;
typedef long XML_Index;

enum XML_Status { XML_STATUS_ERROR = 0, XML_STATUS_OK = 1, XML_STATUS_SUSPENDED = 2 };
enum XML_Error { XML_ERROR_NONE = 0 };

typedef struct { int major, minor, micro; } XML_Expat_Version;
typedef struct { int feature; const XML_LChar *name; long int value; } XML_Feature;
typedef struct { int parsing; XML_Bool finalBuffer; } XML_ParsingStatus;

typedef void (*XML_StartElementHandler)(void *, const XML_Char *, const XML_Char **);
typedef void (*XML_EndElementHandler)(void *, const XML_Char *);
typedef void (*XML_CharacterDataHandler)(void *, const XML_Char *, int);

/* ------------------------- 控制函数 ------------------------- */
extern int expat_dyn_init(const char *);
extern int expat_dyn_libok(void);
extern void expat_dyn_cleanup(void);
extern int expat_dyn_has_symbol(const char *);

/* ------------------------- 被封装函数声明 ------------------------- */
extern const XML_LChar *XML_ExpatVersion(void);
extern XML_Expat_Version XML_ExpatVersionInfo(void);
extern const XML_Feature *XML_GetFeatureList(void);
extern XML_Parser XML_ParserCreate(const XML_Char *);
extern XML_Parser XML_ParserCreateNS(const XML_Char *, XML_Char);
extern enum XML_Status XML_Parse(XML_Parser, const char *, int, int);
extern void XML_ParserFree(XML_Parser);
extern void XML_SetElementHandler(XML_Parser, XML_StartElementHandler, XML_EndElementHandler);
extern void XML_SetCharacterDataHandler(XML_Parser, XML_CharacterDataHandler);
extern enum XML_Error XML_GetErrorCode(XML_Parser);
extern const XML_LChar *XML_ErrorString(enum XML_Error);
extern void XML_GetParsingStatus(XML_Parser, XML_ParsingStatus *);
extern XML_Bool XML_ParserReset(XML_Parser, const XML_Char *);
extern XML_Size XML_GetCurrentLineNumber(XML_Parser);
extern XML_Size XML_GetCurrentColumnNumber(XML_Parser);
extern const XML_Char *XML_GetBase(XML_Parser);

/* ------------------------- 回调 ------------------------- */
static int g_start;
static int g_end;
static int g_text;
static char g_textbuf[64];

static void on_start(void *ud, const XML_Char *name, const XML_Char **atts) {
    (void)ud; (void)name; (void)atts;
    g_start++;
}
static void on_end(void *ud, const XML_Char *name) {
    (void)ud; (void)name;
    g_end++;
}
static void on_text(void *ud, const XML_Char *s, int len) {
    (void)ud;
    if (g_text + len < (int)sizeof g_textbuf) {
        memcpy(g_textbuf + g_text, s, (size_t)len);
        g_text += len;
        g_textbuf[g_text] = 0;
    }
}

/* ------------------------- 83 个导出符号 ------------------------- */
static const char *const k_syms[] = {
    "XML_DefaultCurrent", "XML_ErrorString", "XML_ExpatVersion", "XML_ExpatVersionInfo",
    "XML_ExternalEntityParserCreate", "XML_FreeContentModel", "XML_GetBase", "XML_GetBuffer",
    "XML_GetCurrentByteCount", "XML_GetCurrentByteCount64", "XML_GetCurrentByteIndex",
    "XML_GetCurrentByteIndex64", "XML_GetCurrentColumnNumber", "XML_GetCurrentColumnNumber64",
    "XML_GetCurrentLineNumber", "XML_GetCurrentLineNumber64", "XML_GetErrorCode",
    "XML_GetFeatureList", "XML_GetIdAttributeIndex", "XML_GetInputContext",
    "XML_GetInputContext64", "XML_GetParsingStatus", "XML_GetPropertyBool",
    "XML_GetPropertyDouble", "XML_GetPropertyUInt64", "XML_GetSpecifiedAttributeCount",
    "XML_MemFree", "XML_MemMalloc", "XML_MemRealloc", "XML_Parse", "XML_ParseBuffer",
    "XML_ParserCreate", "XML_ParserCreate_MM", "XML_ParserCreateNS", "XML_ParserFree",
    "XML_ParserReset", "XML_ResumeParser", "XML_SetAllocTrackerActivationThreshold",
    "XML_SetAllocTrackerMaximumAmplification", "XML_SetAttlistDeclHandler", "XML_SetBase",
    "XML_SetBillionLaughsAttackProtectionActivationThreshold",
    "XML_SetBillionLaughsAttackProtectionMaximumAmplification", "XML_SetCdataSectionHandler",
    "XML_SetCharacterDataHandler", "XML_SetCommentHandler", "XML_SetDefaultHandler",
    "XML_SetDefaultHandlerExpand", "XML_SetDoctypeDeclHandler", "XML_SetElementDeclHandler",
    "XML_SetElementHandler", "XML_SetEncoding", "XML_SetEndCdataSectionHandler",
    "XML_SetEndDoctypeDeclHandler", "XML_SetEndElementHandler", "XML_SetEndNamespaceDeclHandler",
    "XML_SetEntityDeclHandler", "XML_SetExternalEntityRefHandler",
    "XML_SetExternalEntityRefHandlerArg", "XML_SetHashSalt", "XML_SetHashSalt16Bytes",
    "XML_SetNamespaceDeclHandler", "XML_SetNotationDeclHandler", "XML_SetNotStandaloneHandler",
    "XML_SetParamEntityParsing", "XML_SetProcessingInstructionHandler", "XML_SetPropertyBool",
    "XML_SetPropertyDouble", "XML_SetPropertyUInt64", "XML_SetReparseDeferralEnabled",
    "XML_SetReturnNSTriplet", "XML_SetSkippedEntityHandler", "XML_SetStartCdataSectionHandler",
    "XML_SetStartDoctypeDeclHandler", "XML_SetStartElementHandler",
    "XML_SetStartNamespaceDeclHandler", "XML_SetUnknownEncodingHandler",
    "XML_SetUnparsedEntityDeclHandler", "XML_SetUserData", "XML_SetXmlDeclHandler",
    "XML_StopParser", "XML_UseForeignDTD", "XML_UseParserAsHandlerArg"
};

int main(int argc, char **argv) {
    int failures = 0;
    int i, bad = 0;
    XML_Parser p;
    XML_Expat_Version vi;

    if (expat_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("expat 不可加载\n");
    failures += CHECK(expat_dyn_libok() != 0);

    /* 1. 版本 */
    printf("expat %s\n", XML_ExpatVersion());
    failures += CHECK(XML_ExpatVersion() != NULL);
    vi = XML_ExpatVersionInfo();
    printf("version info: %d.%d.%d\n", vi.major, vi.minor, vi.micro);
    failures += CHECK(vi.major == 2);

    /* 2. 回调解析 */
    g_start = g_end = g_text = 0;
    g_textbuf[0] = 0;
    p = XML_ParserCreate(NULL);
    failures += CHECK(p != NULL);
    XML_SetElementHandler(p, on_start, on_end);
    XML_SetCharacterDataHandler(p, on_text);
    failures += CHECK(XML_Parse(p, "<root><a>hi</a></root>", (int)strlen("<root><a>hi</a></root>"), 1) == XML_STATUS_OK);
    printf("start=%d end=%d text='%s'\n", g_start, g_end, g_textbuf);
    failures += CHECK(g_start == 2);
    failures += CHECK(g_end == 2);
    failures += CHECK(strcmp(g_textbuf, "hi") == 0);
    XML_ParserFree(p);

    /* 3. 错误路径 */
    p = XML_ParserCreate(NULL);
    failures += CHECK(p != NULL);
    failures += CHECK(XML_Parse(p, "<a><b></a>", (int)strlen("<a><b></a>"), 1) == XML_STATUS_ERROR);
    failures += CHECK(XML_GetErrorCode(p) != XML_ERROR_NONE);
    printf("error: %s\n", XML_ErrorString(XML_GetErrorCode(p)));
    failures += CHECK(XML_ErrorString(XML_GetErrorCode(p)) != NULL);
    XML_ParserFree(p);

    /* 4. 解析状态 + 行列号 + Reset */
    p = XML_ParserCreate(NULL);
    failures += CHECK(p != NULL);
    {
        XML_ParsingStatus st;
        XML_GetParsingStatus(p, &st);
        failures += CHECK(st.parsing == 0);   /* XML_INITIALIZED */
    }
    failures += CHECK(XML_Parse(p, "<a/>", 4, 0) == XML_STATUS_OK);
    failures += CHECK(XML_GetCurrentLineNumber(p) == 1);
    (void)XML_GetCurrentColumnNumber(p);
    failures += CHECK(XML_ParserReset(p, NULL) != 0);
    failures += CHECK(XML_Parse(p, "<b/>", 4, 1) == XML_STATUS_OK);
    XML_ParserFree(p);

    /* 5. 命名空间解析 */
    p = XML_ParserCreateNS(NULL, '|');
    failures += CHECK(p != NULL);
    failures += CHECK(XML_Parse(p, "<x:a xmlns:x='urn:u'/>", (int)strlen("<x:a xmlns:x='urn:u'/>"), 1) == XML_STATUS_OK);
    XML_ParserFree(p);

    /* 6. FeatureList */
    {
        const XML_Feature *f = XML_GetFeatureList();
        int nf = 0;
        failures += CHECK(f != NULL);
        while (f && f->name) { nf++; f++; }
        printf("features: %d\n", nf);
        failures += CHECK(nf > 0);
    }

    /* 7. 全量符号探测（83） */
    for (i = 0; i < (int)(sizeof k_syms / sizeof k_syms[0]); i++) {
        if (!expat_dyn_has_symbol(k_syms[i])) {
            printf("missing symbol: %s\n", k_syms[i]);
            bad++;
        }
    }
    printf("symbols: %d checked, %d missing\n",
           (int)(sizeof k_syms / sizeof k_syms[0]), bad);
    failures += CHECK(bad == 0);
    failures += CHECK((int)(sizeof k_syms / sizeof k_syms[0]) == 83);

    expat_dyn_cleanup();
    if (failures) { printf("%d checks failed\n", failures); return 1; }
    printf("all checks passed\n");
    return 0;
}

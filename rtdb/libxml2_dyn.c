/* ============================================================
 * libxml2_dyn.c - type safe dynamic lib load for libxml2 2.15
 * ============================================================
 * 设计要点：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不需要目标库。
 *   覆盖 libxml2.so 全部 1422 个导出符号：
 *     T 1400（xml* 1312 / html* 62 / xlink* 5 / __xml* getter 21）
 *     数据符号 22（D 15 / R 4 / B 3）
 *   签名与公开结构按官方 v2.15.4 的 46 个 libxml 头文件核对。
 *   本机 soname 为 libxml2.so.16，候选表同时含 .so.2。
 *   2. 每个函数首次调用时 dlsym 缓存；失败打印 stderr 并返回安全值。
 *   3. 加载顺序：显式路径 > LIBXML2_DYN_LIB > LD_LIBRARY_PATH >
 *      soname > .so > Nix > 平台默认；成功后不卸载。
 *   注意：xmlFree / xmlMalloc / xmlStringText / xmlDefaultSAXHandler
 *   等 22 个数据符号以同名真实变量导出，加载后从目标库拷贝值
 *   （见 libxml2_dyn_sync_globals）；可变参数 xmlStrPrintf 转
 *   xmlStrVPrintf 实现。
 *   使用：
 *     libxml2_dyn_init(NULL);
 *     ... 原版 API 直接调用 ...
 *     libxml2_dyn_cleanup();   // 仅复位加载状态，不卸载库
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <termios.h>

/* ============================================================
 * 平台适配层
 * ============================================================ */
#ifdef _WIN32
 #include <windows.h>
 typedef HMODULE lib_handle_t;
 static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
 static void *get_sym(lib_handle_t h, const char *name) {
     return (void *)GetProcAddress(h, name);
 }
 static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *lib_error(void) { return "LoadLibrary failed"; }
 #define PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *lib_handle_t;
 static lib_handle_t load_lib(const char *path) {
     return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
 }
 static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
 static const char *lib_error(void) { return dlerror(); }
 #define PATH_SEP ':'
#endif

/* ============================================================
 * 类型定义（官方头文件机械提取；未列出的 struct 为不透明句柄）
 * ============================================================ */
struct _xmlAttrHashBucket;
struct _xmlAutomata;
struct _xmlAutomataState;
struct _xmlBuf;
struct _xmlCatalog;
struct _xmlDict;
struct _xmlHashTable;
struct _xmlLink;
struct _xmlList;
struct _xmlModule;
struct _xmlMutex;
struct _xmlParserNsData;
struct _xmlPattern;
struct _xmlRMutex;
struct _xmlRegExecCtxt;
struct _xmlRegexp;
struct _xmlRelaxNG;
struct _xmlRelaxNGParserCtxt;
struct _xmlRelaxNGValidCtxt;
struct _xmlSaveCtxt;
struct _xmlSchemaParserCtxt;
struct _xmlSchemaSAXPlug;
struct _xmlSchemaVal;
struct _xmlSchemaValidCtxt;
struct _xmlSchematron;
struct _xmlSchematronParserCtxt;
struct _xmlSchematronValidCtxt;
struct _xmlStartTag;
struct _xmlStreamCtxt;
struct _xmlTextReader;
struct _xmlTextWriter;
struct _xmlValidState;
struct _xmlXIncludeCtxt;
struct _xmlXPathCompExpr;

typedef unsigned char xmlChar;
typedef void (*xmlFreeFunc)(void *mem);
typedef void *(*xmlMallocFunc)(size_t size) __attribute__((alloc_size(1)));
typedef void *(*xmlReallocFunc)(void *mem, size_t size);
typedef char *(*xmlStrdupFunc)(const char *str);
typedef struct _xmlRegexp xmlRegexp;
typedef xmlRegexp *xmlRegexpPtr;
typedef struct _xmlRegExecCtxt xmlRegExecCtxt;
typedef xmlRegExecCtxt *xmlRegExecCtxtPtr;
typedef void (*xmlRegExecCallbacks) (xmlRegExecCtxt *exec, const xmlChar *token, void *transdata, void *inputdata);
typedef struct _xmlParserInputBuffer xmlParserInputBuffer;
typedef xmlParserInputBuffer *xmlParserInputBufferPtr;
typedef struct _xmlOutputBuffer xmlOutputBuffer;
typedef xmlOutputBuffer *xmlOutputBufferPtr;
typedef struct _xmlParserInput xmlParserInput;
typedef xmlParserInput *xmlParserInputPtr;
typedef struct _xmlParserCtxt xmlParserCtxt;
typedef xmlParserCtxt *xmlParserCtxtPtr;
typedef struct _xmlSAXLocator xmlSAXLocator;
typedef xmlSAXLocator *xmlSAXLocatorPtr;
typedef struct _xmlSAXHandler xmlSAXHandler;
typedef xmlSAXHandler *xmlSAXHandlerPtr;
typedef struct _xmlEntity xmlEntity;
typedef xmlEntity *xmlEntityPtr;
typedef enum { XML_BUFFER_ALLOC_DOUBLEIT, XML_BUFFER_ALLOC_EXACT, XML_BUFFER_ALLOC_IMMUTABLE, XML_BUFFER_ALLOC_IO, XML_BUFFER_ALLOC_HYBRID, XML_BUFFER_ALLOC_BOUNDED } xmlBufferAllocationScheme;
typedef struct _xmlBuffer xmlBuffer;
typedef xmlBuffer *xmlBufferPtr;
struct _xmlBuffer { xmlChar *content __attribute__((deprecated)); unsigned int use __attribute__((deprecated)); unsigned int size __attribute__((deprecated)); xmlBufferAllocationScheme alloc __attribute__((deprecated)); xmlChar *contentIO __attribute__((deprecated)); };
typedef struct _xmlBuf xmlBuf;
typedef xmlBuf *xmlBufPtr;
typedef enum { XML_ELEMENT_NODE= 1, XML_ATTRIBUTE_NODE= 2, XML_TEXT_NODE= 3, XML_CDATA_SECTION_NODE= 4, XML_ENTITY_REF_NODE= 5, XML_ENTITY_NODE= 6, XML_PI_NODE= 7, XML_COMMENT_NODE= 8, XML_DOCUMENT_NODE= 9, XML_DOCUMENT_TYPE_NODE= 10, XML_DOCUMENT_FRAG_NODE= 11, XML_NOTATION_NODE= 12, XML_HTML_DOCUMENT_NODE= 13, XML_DTD_NODE= 14, XML_ELEMENT_DECL= 15, XML_ATTRIBUTE_DECL= 16, XML_ENTITY_DECL= 17, XML_NAMESPACE_DECL= 18, XML_XINCLUDE_START= 19, XML_XINCLUDE_END= 20 } xmlElementType;
typedef struct _xmlNotation xmlNotation;
typedef xmlNotation *xmlNotationPtr;
struct _xmlNotation { const xmlChar *name; const xmlChar *PublicID; const xmlChar *SystemID; };
typedef enum { XML_ATTRIBUTE_CDATA = 1, XML_ATTRIBUTE_ID, XML_ATTRIBUTE_IDREF , XML_ATTRIBUTE_IDREFS, XML_ATTRIBUTE_ENTITY, XML_ATTRIBUTE_ENTITIES, XML_ATTRIBUTE_NMTOKEN, XML_ATTRIBUTE_NMTOKENS, XML_ATTRIBUTE_ENUMERATION, XML_ATTRIBUTE_NOTATION } xmlAttributeType;
typedef enum { XML_ATTRIBUTE_NONE = 1, XML_ATTRIBUTE_REQUIRED, XML_ATTRIBUTE_IMPLIED, XML_ATTRIBUTE_FIXED } xmlAttributeDefault;
typedef struct _xmlEnumeration xmlEnumeration;
typedef xmlEnumeration *xmlEnumerationPtr;
struct _xmlEnumeration { struct _xmlEnumeration *next __attribute__((deprecated)); const xmlChar *name __attribute__((deprecated)); };
typedef struct _xmlAttribute xmlAttribute;
typedef xmlAttribute *xmlAttributePtr;
struct _xmlAttribute { void *_private; xmlElementType type; const xmlChar *name; struct _xmlNode *children; struct _xmlNode *last; struct _xmlDtd *parent; struct _xmlNode *next; struct _xmlNode *prev; struct _xmlDoc *doc; struct _xmlAttribute *nexth; xmlAttributeType atype; xmlAttributeDefault def; xmlChar *defaultValue; xmlEnumeration *tree __attribute__((deprecated)); const xmlChar *prefix; const xmlChar *elem; };
typedef enum { XML_ELEMENT_CONTENT_PCDATA = 1, XML_ELEMENT_CONTENT_ELEMENT, XML_ELEMENT_CONTENT_SEQ, XML_ELEMENT_CONTENT_OR } xmlElementContentType;
typedef enum { XML_ELEMENT_CONTENT_ONCE = 1, XML_ELEMENT_CONTENT_OPT, XML_ELEMENT_CONTENT_MULT, XML_ELEMENT_CONTENT_PLUS } xmlElementContentOccur;
typedef struct _xmlElementContent xmlElementContent;
typedef xmlElementContent *xmlElementContentPtr;
struct _xmlElementContent { xmlElementContentType type __attribute__((deprecated)); xmlElementContentOccur ocur __attribute__((deprecated)); const xmlChar *name __attribute__((deprecated)); struct _xmlElementContent *c1 __attribute__((deprecated)); struct _xmlElementContent *c2 __attribute__((deprecated)); struct _xmlElementContent *parent __attribute__((deprecated)); const xmlChar *prefix __attribute__((deprecated)); };
typedef enum { XML_ELEMENT_TYPE_UNDEFINED = 0, XML_ELEMENT_TYPE_EMPTY = 1, XML_ELEMENT_TYPE_ANY, XML_ELEMENT_TYPE_MIXED, XML_ELEMENT_TYPE_ELEMENT } xmlElementTypeVal;
typedef struct _xmlElement xmlElement;
typedef xmlElement *xmlElementPtr;
struct _xmlElement { void *_private; xmlElementType type; const xmlChar *name; struct _xmlNode *children; struct _xmlNode *last; struct _xmlDtd *parent; struct _xmlNode *next; struct _xmlNode *prev; struct _xmlDoc *doc; xmlElementTypeVal etype; xmlElementContent *content; xmlAttribute *attributes; const xmlChar *prefix; xmlRegexp *contModel __attribute__((deprecated)); };
typedef xmlElementType xmlNsType;
typedef struct _xmlNs xmlNs;
typedef xmlNs *xmlNsPtr;
struct _xmlNs { struct _xmlNs *next; xmlNsType type; const xmlChar *href; const xmlChar *prefix; void *_private; struct _xmlDoc *context __attribute__((deprecated)); };
typedef struct _xmlDtd xmlDtd;
typedef xmlDtd *xmlDtdPtr;
struct _xmlDtd { void *_private; xmlElementType type; const xmlChar *name; struct _xmlNode *children; struct _xmlNode *last; struct _xmlDoc *parent; struct _xmlNode *next; struct _xmlNode *prev; struct _xmlDoc *doc; void *notations; void *elements; void *attributes; void *entities; xmlChar *ExternalID; xmlChar *SystemID; void *pentities; };
typedef struct _xmlAttr xmlAttr;
typedef xmlAttr *xmlAttrPtr;
struct _xmlAttr { void *_private; xmlElementType type; const xmlChar *name; struct _xmlNode *children; struct _xmlNode *last; struct _xmlNode *parent; struct _xmlAttr *next; struct _xmlAttr *prev; struct _xmlDoc *doc; xmlNs *ns; xmlAttributeType atype; void *psvi; struct _xmlID *id __attribute__((deprecated)); };
typedef struct _xmlID xmlID;
typedef xmlID *xmlIDPtr;
struct _xmlID { struct _xmlID *next __attribute__((deprecated)); xmlChar *value __attribute__((deprecated)); xmlAttr *attr __attribute__((deprecated)); const xmlChar *name __attribute__((deprecated)); int lineno __attribute__((deprecated)); struct _xmlDoc *doc __attribute__((deprecated)); };
typedef struct _xmlRef xmlRef;
typedef xmlRef *xmlRefPtr;
struct _xmlRef { struct _xmlRef *next __attribute__((deprecated)); const xmlChar *value __attribute__((deprecated)); xmlAttr *attr __attribute__((deprecated)); const xmlChar *name __attribute__((deprecated)); int lineno __attribute__((deprecated)); };
typedef struct _xmlNode xmlNode;
typedef xmlNode *xmlNodePtr;
struct _xmlNode { void *_private; xmlElementType type; const xmlChar *name; struct _xmlNode *children; struct _xmlNode *last; struct _xmlNode *parent; struct _xmlNode *next; struct _xmlNode *prev; struct _xmlDoc *doc; xmlNs *ns; xmlChar *content; struct _xmlAttr *properties; xmlNs *nsDef; void *psvi; unsigned short line; unsigned short extra; };
typedef enum { XML_DOC_WELLFORMED = 1<<0, XML_DOC_NSVALID = 1<<1, XML_DOC_OLD10 = 1<<2, XML_DOC_DTDVALID = 1<<3, XML_DOC_XINCLUDE = 1<<4, XML_DOC_USERBUILT = 1<<5, XML_DOC_INTERNAL = 1<<6, XML_DOC_HTML = 1<<7 } xmlDocProperties;
typedef struct _xmlDoc xmlDoc;
typedef xmlDoc *xmlDocPtr;
struct _xmlDoc { void *_private; xmlElementType type; char *name; struct _xmlNode *children; struct _xmlNode *last; struct _xmlNode *parent; struct _xmlNode *next; struct _xmlNode *prev; struct _xmlDoc *doc; int compression; int standalone; struct _xmlDtd *intSubset; struct _xmlDtd *extSubset; struct _xmlNs *oldNs; xmlChar *version; xmlChar *encoding; void *ids; void *refs __attribute__((deprecated)); xmlChar *URL; int charset; struct _xmlDict *dict; void *psvi; int parseFlags; int properties; };
typedef struct _xmlDOMWrapCtxt xmlDOMWrapCtxt;
typedef xmlDOMWrapCtxt *xmlDOMWrapCtxtPtr;
typedef xmlNs *(*xmlDOMWrapAcquireNsFunction) (xmlDOMWrapCtxt *ctxt, xmlNode *node, const xmlChar *nsName, const xmlChar *nsPrefix);
struct _xmlDOMWrapCtxt { void * _private; int type; void * namespaceMap; xmlDOMWrapAcquireNsFunction getNsForNodeFunc; };
typedef void (*xmlRegisterNodeFunc) (xmlNode *node);
typedef void (*xmlDeregisterNodeFunc) (xmlNode *node);
typedef struct _xmlDict xmlDict;
typedef xmlDict *xmlDictPtr;
typedef struct _xmlHashTable xmlHashTable;
typedef xmlHashTable *xmlHashTablePtr;
typedef void (*xmlHashDeallocator)(void *payload, const xmlChar *name);
typedef void *(*xmlHashCopier)(void *payload, const xmlChar *name);
typedef void (*xmlHashScanner)(void *payload, void *data, const xmlChar *name);
typedef void (*xmlHashScannerFull)(void *payload, void *data, const xmlChar *name, const xmlChar *name2, const xmlChar *name3);
typedef enum { XML_ERR_NONE = 0, XML_ERR_WARNING = 1, XML_ERR_ERROR = 2, XML_ERR_FATAL = 3 } xmlErrorLevel;
typedef enum { XML_FROM_NONE = 0, XML_FROM_PARSER, XML_FROM_TREE, XML_FROM_NAMESPACE, XML_FROM_DTD, XML_FROM_HTML, XML_FROM_MEMORY, XML_FROM_OUTPUT, XML_FROM_IO, XML_FROM_FTP, XML_FROM_HTTP, XML_FROM_XINCLUDE, XML_FROM_XPATH, XML_FROM_XPOINTER, XML_FROM_REGEXP, XML_FROM_DATATYPE, XML_FROM_SCHEMASP, XML_FROM_SCHEMASV, XML_FROM_RELAXNGP, XML_FROM_RELAXNGV, XML_FROM_CATALOG, XML_FROM_C14N, XML_FROM_XSLT, XML_FROM_VALID, XML_FROM_CHECK, XML_FROM_WRITER, XML_FROM_MODULE, XML_FROM_I18N, XML_FROM_SCHEMATRONV, XML_FROM_BUFFER, XML_FROM_URI } xmlErrorDomain;
typedef struct _xmlError xmlError;
typedef xmlError *xmlErrorPtr;
struct _xmlError { int domain; int code; char *message; xmlErrorLevel level; char *file; int line; char *str1; char *str2; char *str3; int int1; int int2; void *ctxt; void *node; };
typedef enum { XML_ERR_OK = 0, XML_ERR_INTERNAL_ERROR, XML_ERR_NO_MEMORY, XML_ERR_DOCUMENT_START, XML_ERR_DOCUMENT_EMPTY, XML_ERR_DOCUMENT_END, XML_ERR_INVALID_HEX_CHARREF, XML_ERR_INVALID_DEC_CHARREF, XML_ERR_INVALID_CHARREF, XML_ERR_INVALID_CHAR, XML_ERR_CHARREF_AT_EOF, XML_ERR_CHARREF_IN_PROLOG, XML_ERR_CHARREF_IN_EPILOG, XML_ERR_CHARREF_IN_DTD, XML_ERR_ENTITYREF_AT_EOF, XML_ERR_ENTITYREF_IN_PROLOG, XML_ERR_ENTITYREF_IN_EPILOG, XML_ERR_ENTITYREF_IN_DTD, XML_ERR_PEREF_AT_EOF, XML_ERR_PEREF_IN_PROLOG, XML_ERR_PEREF_IN_EPILOG, XML_ERR_PEREF_IN_INT_SUBSET, XML_ERR_ENTITYREF_NO_NAME, XML_ERR_ENTITYREF_SEMICOL_MISSING, XML_ERR_PEREF_NO_NAME, XML_ERR_PEREF_SEMICOL_MISSING, XML_ERR_UNDECLARED_ENTITY, XML_WAR_UNDECLARED_ENTITY, XML_ERR_UNPARSED_ENTITY, XML_ERR_ENTITY_IS_EXTERNAL, XML_ERR_ENTITY_IS_PARAMETER, XML_ERR_UNKNOWN_ENCODING, XML_ERR_UNSUPPORTED_ENCODING, XML_ERR_STRING_NOT_STARTED, XML_ERR_STRING_NOT_CLOSED, XML_ERR_NS_DECL_ERROR, XML_ERR_ENTITY_NOT_STARTED, XML_ERR_ENTITY_NOT_FINISHED, XML_ERR_LT_IN_ATTRIBUTE, XML_ERR_ATTRIBUTE_NOT_STARTED, XML_ERR_ATTRIBUTE_NOT_FINISHED, XML_ERR_ATTRIBUTE_WITHOUT_VALUE, XML_ERR_ATTRIBUTE_REDEFINED, XML_ERR_LITERAL_NOT_STARTED, XML_ERR_LITERAL_NOT_FINISHED, XML_ERR_COMMENT_NOT_FINISHED, XML_ERR_PI_NOT_STARTED, XML_ERR_PI_NOT_FINISHED, XML_ERR_NOTATION_NOT_STARTED, XML_ERR_NOTATION_NOT_FINISHED, XML_ERR_ATTLIST_NOT_STARTED, XML_ERR_ATTLIST_NOT_FINISHED, XML_ERR_MIXED_NOT_STARTED, XML_ERR_MIXED_NOT_FINISHED, XML_ERR_ELEMCONTENT_NOT_STARTED, XML_ERR_ELEMCONTENT_NOT_FINISHED, XML_ERR_XMLDECL_NOT_STARTED, XML_ERR_XMLDECL_NOT_FINISHED, XML_ERR_CONDSEC_NOT_STARTED, XML_ERR_CONDSEC_NOT_FINISHED, XML_ERR_EXT_SUBSET_NOT_FINISHED, XML_ERR_DOCTYPE_NOT_FINISHED, XML_ERR_MISPLACED_CDATA_END, XML_ERR_CDATA_NOT_FINISHED, XML_ERR_RESERVED_XML_NAME, XML_ERR_SPACE_REQUIRED, XML_ERR_SEPARATOR_REQUIRED, XML_ERR_NMTOKEN_REQUIRED, XML_ERR_NAME_REQUIRED, XML_ERR_PCDATA_REQUIRED, XML_ERR_URI_REQUIRED, XML_ERR_PUBID_REQUIRED, XML_ERR_LT_REQUIRED, XML_ERR_GT_REQUIRED, XML_ERR_LTSLASH_REQUIRED, XML_ERR_EQUAL_REQUIRED, XML_ERR_TAG_NAME_MISMATCH, XML_ERR_TAG_NOT_FINISHED, XML_ERR_STANDALONE_VALUE, XML_ERR_ENCODING_NAME, XML_ERR_HYPHEN_IN_COMMENT, XML_ERR_INVALID_ENCODING, XML_ERR_EXT_ENTITY_STANDALONE, XML_ERR_CONDSEC_INVALID, XML_ERR_VALUE_REQUIRED, XML_ERR_NOT_WELL_BALANCED, XML_ERR_EXTRA_CONTENT, XML_ERR_ENTITY_CHAR_ERROR, XML_ERR_ENTITY_PE_INTERNAL, XML_ERR_ENTITY_LOOP, XML_ERR_ENTITY_BOUNDARY, XML_ERR_INVALID_URI, XML_ERR_URI_FRAGMENT, XML_WAR_CATALOG_PI, XML_ERR_NO_DTD, XML_ERR_CONDSEC_INVALID_KEYWORD, XML_ERR_VERSION_MISSING, XML_WAR_UNKNOWN_VERSION, XML_WAR_LANG_VALUE, XML_WAR_NS_URI, XML_WAR_NS_URI_RELATIVE, XML_ERR_MISSING_ENCODING, XML_WAR_SPACE_VALUE, XML_ERR_NOT_STANDALONE, XML_ERR_ENTITY_PROCESSING, XML_ERR_NOTATION_PROCESSING, XML_WAR_NS_COLUMN, XML_WAR_ENTITY_REDEFINED, XML_ERR_UNKNOWN_VERSION, XML_ERR_VERSION_MISMATCH, XML_ERR_NAME_TOO_LONG, XML_ERR_USER_STOP, XML_ERR_COMMENT_ABRUPTLY_ENDED, XML_WAR_ENCODING_MISMATCH, XML_ERR_RESOURCE_LIMIT, XML_ERR_ARGUMENT, XML_ERR_SYSTEM, XML_ERR_REDECL_PREDEF_ENTITY, XML_ERR_INT_SUBSET_NOT_FINISHED, XML_NS_ERR_XML_NAMESPACE = 200, XML_NS_ERR_UNDEFINED_NAMESPACE, XML_NS_ERR_QNAME, XML_NS_ERR_ATTRIBUTE_REDEFINED, XML_NS_ERR_EMPTY, XML_NS_ERR_COLON, XML_DTD_ATTRIBUTE_DEFAULT = 500, XML_DTD_ATTRIBUTE_REDEFINED, XML_DTD_ATTRIBUTE_VALUE, XML_DTD_CONTENT_ERROR, XML_DTD_CONTENT_MODEL, XML_DTD_CONTENT_NOT_DETERMINIST, XML_DTD_DIFFERENT_PREFIX, XML_DTD_ELEM_DEFAULT_NAMESPACE, XML_DTD_ELEM_NAMESPACE, XML_DTD_ELEM_REDEFINED, XML_DTD_EMPTY_NOTATION, XML_DTD_ENTITY_TYPE, XML_DTD_ID_FIXED, XML_DTD_ID_REDEFINED, XML_DTD_ID_SUBSET, XML_DTD_INVALID_CHILD, XML_DTD_INVALID_DEFAULT, XML_DTD_LOAD_ERROR, XML_DTD_MISSING_ATTRIBUTE, XML_DTD_MIXED_CORRUPT, XML_DTD_MULTIPLE_ID, XML_DTD_NO_DOC, XML_DTD_NO_DTD, XML_DTD_NO_ELEM_NAME, XML_DTD_NO_PREFIX, XML_DTD_NO_ROOT, XML_DTD_NOTATION_REDEFINED, XML_DTD_NOTATION_VALUE, XML_DTD_NOT_EMPTY, XML_DTD_NOT_PCDATA, XML_DTD_NOT_STANDALONE, XML_DTD_ROOT_NAME, XML_DTD_STANDALONE_WHITE_SPACE, XML_DTD_UNKNOWN_ATTRIBUTE, XML_DTD_UNKNOWN_ELEM, XML_DTD_UNKNOWN_ENTITY, XML_DTD_UNKNOWN_ID, XML_DTD_UNKNOWN_NOTATION, XML_DTD_STANDALONE_DEFAULTED, XML_DTD_XMLID_VALUE, XML_DTD_XMLID_TYPE, XML_DTD_DUP_TOKEN, XML_HTML_STRUCURE_ERROR = 800, XML_HTML_UNKNOWN_TAG, XML_HTML_INCORRECTLY_OPENED_COMMENT, XML_RNGP_ANYNAME_ATTR_ANCESTOR = 1000, XML_RNGP_ATTR_CONFLICT, XML_RNGP_ATTRIBUTE_CHILDREN, XML_RNGP_ATTRIBUTE_CONTENT, XML_RNGP_ATTRIBUTE_EMPTY, XML_RNGP_ATTRIBUTE_NOOP, XML_RNGP_CHOICE_CONTENT, XML_RNGP_CHOICE_EMPTY, XML_RNGP_CREATE_FAILURE, XML_RNGP_DATA_CONTENT, XML_RNGP_DEF_CHOICE_AND_INTERLEAVE, XML_RNGP_DEFINE_CREATE_FAILED, XML_RNGP_DEFINE_EMPTY, XML_RNGP_DEFINE_MISSING, XML_RNGP_DEFINE_NAME_MISSING, XML_RNGP_ELEM_CONTENT_EMPTY, XML_RNGP_ELEM_CONTENT_ERROR, XML_RNGP_ELEMENT_EMPTY, XML_RNGP_ELEMENT_CONTENT, XML_RNGP_ELEMENT_NAME, XML_RNGP_ELEMENT_NO_CONTENT, XML_RNGP_ELEM_TEXT_CONFLICT, XML_RNGP_EMPTY, XML_RNGP_EMPTY_CONSTRUCT, XML_RNGP_EMPTY_CONTENT, XML_RNGP_EMPTY_NOT_EMPTY, XML_RNGP_ERROR_TYPE_LIB, XML_RNGP_EXCEPT_EMPTY, XML_RNGP_EXCEPT_MISSING, XML_RNGP_EXCEPT_MULTIPLE, XML_RNGP_EXCEPT_NO_CONTENT, XML_RNGP_EXTERNALREF_EMTPY, XML_RNGP_EXTERNAL_REF_FAILURE, XML_RNGP_EXTERNALREF_RECURSE, XML_RNGP_FORBIDDEN_ATTRIBUTE, XML_RNGP_FOREIGN_ELEMENT, XML_RNGP_GRAMMAR_CONTENT, XML_RNGP_GRAMMAR_EMPTY, XML_RNGP_GRAMMAR_MISSING, XML_RNGP_GRAMMAR_NO_START, XML_RNGP_GROUP_ATTR_CONFLICT, XML_RNGP_HREF_ERROR, XML_RNGP_INCLUDE_EMPTY, XML_RNGP_INCLUDE_FAILURE, XML_RNGP_INCLUDE_RECURSE, XML_RNGP_INTERLEAVE_ADD, XML_RNGP_INTERLEAVE_CREATE_FAILED, XML_RNGP_INTERLEAVE_EMPTY, XML_RNGP_INTERLEAVE_NO_CONTENT, XML_RNGP_INVALID_DEFINE_NAME, XML_RNGP_INVALID_URI, XML_RNGP_INVALID_VALUE, XML_RNGP_MISSING_HREF, XML_RNGP_NAME_MISSING, XML_RNGP_NEED_COMBINE, XML_RNGP_NOTALLOWED_NOT_EMPTY, XML_RNGP_NSNAME_ATTR_ANCESTOR, XML_RNGP_NSNAME_NO_NS, XML_RNGP_PARAM_FORBIDDEN, XML_RNGP_PARAM_NAME_MISSING, XML_RNGP_PARENTREF_CREATE_FAILED, XML_RNGP_PARENTREF_NAME_INVALID, XML_RNGP_PARENTREF_NO_NAME, XML_RNGP_PARENTREF_NO_PARENT, XML_RNGP_PARENTREF_NOT_EMPTY, XML_RNGP_PARSE_ERROR, XML_RNGP_PAT_ANYNAME_EXCEPT_ANYNAME, XML_RNGP_PAT_ATTR_ATTR, XML_RNGP_PAT_ATTR_ELEM, XML_RNGP_PAT_DATA_EXCEPT_ATTR, XML_RNGP_PAT_DATA_EXCEPT_ELEM, XML_RNGP_PAT_DATA_EXCEPT_EMPTY, XML_RNGP_PAT_DATA_EXCEPT_GROUP, XML_RNGP_PAT_DATA_EXCEPT_INTERLEAVE, XML_RNGP_PAT_DATA_EXCEPT_LIST, XML_RNGP_PAT_DATA_EXCEPT_ONEMORE, XML_RNGP_PAT_DATA_EXCEPT_REF, XML_RNGP_PAT_DATA_EXCEPT_TEXT, XML_RNGP_PAT_LIST_ATTR, XML_RNGP_PAT_LIST_ELEM, XML_RNGP_PAT_LIST_INTERLEAVE, XML_RNGP_PAT_LIST_LIST, XML_RNGP_PAT_LIST_REF, XML_RNGP_PAT_LIST_TEXT, XML_RNGP_PAT_NSNAME_EXCEPT_ANYNAME, XML_RNGP_PAT_NSNAME_EXCEPT_NSNAME, XML_RNGP_PAT_ONEMORE_GROUP_ATTR, XML_RNGP_PAT_ONEMORE_INTERLEAVE_ATTR, XML_RNGP_PAT_START_ATTR, XML_RNGP_PAT_START_DATA, XML_RNGP_PAT_START_EMPTY, XML_RNGP_PAT_START_GROUP, XML_RNGP_PAT_START_INTERLEAVE, XML_RNGP_PAT_START_LIST, XML_RNGP_PAT_START_ONEMORE, XML_RNGP_PAT_START_TEXT, XML_RNGP_PAT_START_VALUE, XML_RNGP_PREFIX_UNDEFINED, XML_RNGP_REF_CREATE_FAILED, XML_RNGP_REF_CYCLE, XML_RNGP_REF_NAME_INVALID, XML_RNGP_REF_NO_DEF, XML_RNGP_REF_NO_NAME, XML_RNGP_REF_NOT_EMPTY, XML_RNGP_START_CHOICE_AND_INTERLEAVE, XML_RNGP_START_CONTENT, XML_RNGP_START_EMPTY, XML_RNGP_START_MISSING, XML_RNGP_TEXT_EXPECTED, XML_RNGP_TEXT_HAS_CHILD, XML_RNGP_TYPE_MISSING, XML_RNGP_TYPE_NOT_FOUND, XML_RNGP_TYPE_VALUE, XML_RNGP_UNKNOWN_ATTRIBUTE, XML_RNGP_UNKNOWN_COMBINE, XML_RNGP_UNKNOWN_CONSTRUCT, XML_RNGP_UNKNOWN_TYPE_LIB, XML_RNGP_URI_FRAGMENT, XML_RNGP_URI_NOT_ABSOLUTE, XML_RNGP_VALUE_EMPTY, XML_RNGP_VALUE_NO_CONTENT, XML_RNGP_XMLNS_NAME, XML_RNGP_XML_NS, XML_XPATH_EXPRESSION_OK = 1200, XML_XPATH_NUMBER_ERROR, XML_XPATH_UNFINISHED_LITERAL_ERROR, XML_XPATH_START_LITERAL_ERROR, XML_XPATH_VARIABLE_REF_ERROR, XML_XPATH_UNDEF_VARIABLE_ERROR, XML_XPATH_INVALID_PREDICATE_ERROR, XML_XPATH_EXPR_ERROR, XML_XPATH_UNCLOSED_ERROR, XML_XPATH_UNKNOWN_FUNC_ERROR, XML_XPATH_INVALID_OPERAND, XML_XPATH_INVALID_TYPE, XML_XPATH_INVALID_ARITY, XML_XPATH_INVALID_CTXT_SIZE, XML_XPATH_INVALID_CTXT_POSITION, XML_XPATH_MEMORY_ERROR, XML_XPTR_SYNTAX_ERROR, XML_XPTR_RESOURCE_ERROR, XML_XPTR_SUB_RESOURCE_ERROR, XML_XPATH_UNDEF_PREFIX_ERROR, XML_XPATH_ENCODING_ERROR, XML_XPATH_INVALID_CHAR_ERROR, XML_TREE_INVALID_HEX = 1300, XML_TREE_INVALID_DEC, XML_TREE_UNTERMINATED_ENTITY, XML_TREE_NOT_UTF8, XML_SAVE_NOT_UTF8 = 1400, XML_SAVE_CHAR_INVALID, XML_SAVE_NO_DOCTYPE, XML_SAVE_UNKNOWN_ENCODING, XML_REGEXP_COMPILE_ERROR = 1450, XML_IO_UNKNOWN = 1500, XML_IO_EACCES, XML_IO_EAGAIN, XML_IO_EBADF, XML_IO_EBADMSG, XML_IO_EBUSY, XML_IO_ECANCELED, XML_IO_ECHILD, XML_IO_EDEADLK, XML_IO_EDOM, XML_IO_EEXIST, XML_IO_EFAULT, XML_IO_EFBIG, XML_IO_EINPROGRESS, XML_IO_EINTR, XML_IO_EINVAL, XML_IO_EIO, XML_IO_EISDIR, XML_IO_EMFILE, XML_IO_EMLINK, XML_IO_EMSGSIZE, XML_IO_ENAMETOOLONG, XML_IO_ENFILE, XML_IO_ENODEV, XML_IO_ENOENT, XML_IO_ENOEXEC, XML_IO_ENOLCK, XML_IO_ENOMEM, XML_IO_ENOSPC, XML_IO_ENOSYS, XML_IO_ENOTDIR, XML_IO_ENOTEMPTY, XML_IO_ENOTSUP, XML_IO_ENOTTY, XML_IO_ENXIO, XML_IO_EPERM, XML_IO_EPIPE, XML_IO_ERANGE, XML_IO_EROFS, XML_IO_ESPIPE, XML_IO_ESRCH, XML_IO_ETIMEDOUT, XML_IO_EXDEV, XML_IO_NETWORK_ATTEMPT, XML_IO_ENCODER, XML_IO_FLUSH, XML_IO_WRITE, XML_IO_NO_INPUT, XML_IO_BUFFER_FULL, XML_IO_LOAD_ERROR, XML_IO_ENOTSOCK, XML_IO_EISCONN, XML_IO_ECONNREFUSED, XML_IO_ENETUNREACH, XML_IO_EADDRINUSE, XML_IO_EALREADY, XML_IO_EAFNOSUPPORT, XML_IO_UNSUPPORTED_PROTOCOL, XML_XINCLUDE_RECURSION=1600, XML_XINCLUDE_PARSE_VALUE, XML_XINCLUDE_ENTITY_DEF_MISMATCH, XML_XINCLUDE_NO_HREF, XML_XINCLUDE_NO_FALLBACK, XML_XINCLUDE_HREF_URI, XML_XINCLUDE_TEXT_FRAGMENT, XML_XINCLUDE_TEXT_DOCUMENT, XML_XINCLUDE_INVALID_CHAR, XML_XINCLUDE_BUILD_FAILED, XML_XINCLUDE_UNKNOWN_ENCODING, XML_XINCLUDE_MULTIPLE_ROOT, XML_XINCLUDE_XPTR_FAILED, XML_XINCLUDE_XPTR_RESULT, XML_XINCLUDE_INCLUDE_IN_INCLUDE, XML_XINCLUDE_FALLBACKS_IN_INCLUDE, XML_XINCLUDE_FALLBACK_NOT_IN_INCLUDE, XML_XINCLUDE_DEPRECATED_NS, XML_XINCLUDE_FRAGMENT_ID, XML_CATALOG_MISSING_ATTR = 1650, XML_CATALOG_ENTRY_BROKEN, XML_CATALOG_PREFER_VALUE, XML_CATALOG_NOT_CATALOG, XML_CATALOG_RECURSION, XML_SCHEMAP_PREFIX_UNDEFINED = 1700, XML_SCHEMAP_ATTRFORMDEFAULT_VALUE, XML_SCHEMAP_ATTRGRP_NONAME_NOREF, XML_SCHEMAP_ATTR_NONAME_NOREF, XML_SCHEMAP_COMPLEXTYPE_NONAME_NOREF, XML_SCHEMAP_ELEMFORMDEFAULT_VALUE, XML_SCHEMAP_ELEM_NONAME_NOREF, XML_SCHEMAP_EXTENSION_NO_BASE, XML_SCHEMAP_FACET_NO_VALUE, XML_SCHEMAP_FAILED_BUILD_IMPORT, XML_SCHEMAP_GROUP_NONAME_NOREF, XML_SCHEMAP_IMPORT_NAMESPACE_NOT_URI, XML_SCHEMAP_IMPORT_REDEFINE_NSNAME, XML_SCHEMAP_IMPORT_SCHEMA_NOT_URI, XML_SCHEMAP_INVALID_BOOLEAN, XML_SCHEMAP_INVALID_ENUM, XML_SCHEMAP_INVALID_FACET, XML_SCHEMAP_INVALID_FACET_VALUE, XML_SCHEMAP_INVALID_MAXOCCURS, XML_SCHEMAP_INVALID_MINOCCURS, XML_SCHEMAP_INVALID_REF_AND_SUBTYPE, XML_SCHEMAP_INVALID_WHITE_SPACE, XML_SCHEMAP_NOATTR_NOREF, XML_SCHEMAP_NOTATION_NO_NAME, XML_SCHEMAP_NOTYPE_NOREF, XML_SCHEMAP_REF_AND_SUBTYPE, XML_SCHEMAP_RESTRICTION_NONAME_NOREF, XML_SCHEMAP_SIMPLETYPE_NONAME, XML_SCHEMAP_TYPE_AND_SUBTYPE, XML_SCHEMAP_UNKNOWN_ALL_CHILD, XML_SCHEMAP_UNKNOWN_ANYATTRIBUTE_CHILD, XML_SCHEMAP_UNKNOWN_ATTR_CHILD, XML_SCHEMAP_UNKNOWN_ATTRGRP_CHILD, XML_SCHEMAP_UNKNOWN_ATTRIBUTE_GROUP, XML_SCHEMAP_UNKNOWN_BASE_TYPE, XML_SCHEMAP_UNKNOWN_CHOICE_CHILD, XML_SCHEMAP_UNKNOWN_COMPLEXCONTENT_CHILD, XML_SCHEMAP_UNKNOWN_COMPLEXTYPE_CHILD, XML_SCHEMAP_UNKNOWN_ELEM_CHILD, XML_SCHEMAP_UNKNOWN_EXTENSION_CHILD, XML_SCHEMAP_UNKNOWN_FACET_CHILD, XML_SCHEMAP_UNKNOWN_FACET_TYPE, XML_SCHEMAP_UNKNOWN_GROUP_CHILD, XML_SCHEMAP_UNKNOWN_IMPORT_CHILD, XML_SCHEMAP_UNKNOWN_LIST_CHILD, XML_SCHEMAP_UNKNOWN_NOTATION_CHILD, XML_SCHEMAP_UNKNOWN_PROCESSCONTENT_CHILD, XML_SCHEMAP_UNKNOWN_REF, XML_SCHEMAP_UNKNOWN_RESTRICTION_CHILD, XML_SCHEMAP_UNKNOWN_SCHEMAS_CHILD, XML_SCHEMAP_UNKNOWN_SEQUENCE_CHILD, XML_SCHEMAP_UNKNOWN_SIMPLECONTENT_CHILD, XML_SCHEMAP_UNKNOWN_SIMPLETYPE_CHILD, XML_SCHEMAP_UNKNOWN_TYPE, XML_SCHEMAP_UNKNOWN_UNION_CHILD, XML_SCHEMAP_ELEM_DEFAULT_FIXED, XML_SCHEMAP_REGEXP_INVALID, XML_SCHEMAP_FAILED_LOAD, XML_SCHEMAP_NOTHING_TO_PARSE, XML_SCHEMAP_NOROOT, XML_SCHEMAP_REDEFINED_GROUP, XML_SCHEMAP_REDEFINED_TYPE, XML_SCHEMAP_REDEFINED_ELEMENT, XML_SCHEMAP_REDEFINED_ATTRGROUP, XML_SCHEMAP_REDEFINED_ATTR, XML_SCHEMAP_REDEFINED_NOTATION, XML_SCHEMAP_FAILED_PARSE, XML_SCHEMAP_UNKNOWN_PREFIX, XML_SCHEMAP_DEF_AND_PREFIX, XML_SCHEMAP_UNKNOWN_INCLUDE_CHILD, XML_SCHEMAP_INCLUDE_SCHEMA_NOT_URI, XML_SCHEMAP_INCLUDE_SCHEMA_NO_URI, XML_SCHEMAP_NOT_SCHEMA, XML_SCHEMAP_UNKNOWN_MEMBER_TYPE, XML_SCHEMAP_INVALID_ATTR_USE, XML_SCHEMAP_RECURSIVE, XML_SCHEMAP_SUPERNUMEROUS_LIST_ITEM_TYPE, XML_SCHEMAP_INVALID_ATTR_COMBINATION, XML_SCHEMAP_INVALID_ATTR_INLINE_COMBINATION, XML_SCHEMAP_MISSING_SIMPLETYPE_CHILD, XML_SCHEMAP_INVALID_ATTR_NAME, XML_SCHEMAP_REF_AND_CONTENT, XML_SCHEMAP_CT_PROPS_CORRECT_1, XML_SCHEMAP_CT_PROPS_CORRECT_2, XML_SCHEMAP_CT_PROPS_CORRECT_3, XML_SCHEMAP_CT_PROPS_CORRECT_4, XML_SCHEMAP_CT_PROPS_CORRECT_5, XML_SCHEMAP_DERIVATION_OK_RESTRICTION_1, XML_SCHEMAP_DERIVATION_OK_RESTRICTION_2_1_1, XML_SCHEMAP_DERIVATION_OK_RESTRICTION_2_1_2, XML_SCHEMAP_DERIVATION_OK_RESTRICTION_2_2, XML_SCHEMAP_DERIVATION_OK_RESTRICTION_3, XML_SCHEMAP_WILDCARD_INVALID_NS_MEMBER, XML_SCHEMAP_INTERSECTION_NOT_EXPRESSIBLE, XML_SCHEMAP_UNION_NOT_EXPRESSIBLE, XML_SCHEMAP_SRC_IMPORT_3_1, XML_SCHEMAP_SRC_IMPORT_3_2, XML_SCHEMAP_DERIVATION_OK_RESTRICTION_4_1, XML_SCHEMAP_DERIVATION_OK_RESTRICTION_4_2, XML_SCHEMAP_DERIVATION_OK_RESTRICTION_4_3, XML_SCHEMAP_COS_CT_EXTENDS_1_3, XML_SCHEMAV_NOROOT = 1801, XML_SCHEMAV_UNDECLAREDELEM, XML_SCHEMAV_NOTTOPLEVEL, XML_SCHEMAV_MISSING, XML_SCHEMAV_WRONGELEM, XML_SCHEMAV_NOTYPE, XML_SCHEMAV_NOROLLBACK, XML_SCHEMAV_ISABSTRACT, XML_SCHEMAV_NOTEMPTY, XML_SCHEMAV_ELEMCONT, XML_SCHEMAV_HAVEDEFAULT, XML_SCHEMAV_NOTNILLABLE, XML_SCHEMAV_EXTRACONTENT, XML_SCHEMAV_INVALIDATTR, XML_SCHEMAV_INVALIDELEM, XML_SCHEMAV_NOTDETERMINIST, XML_SCHEMAV_CONSTRUCT, XML_SCHEMAV_INTERNAL, XML_SCHEMAV_NOTSIMPLE, XML_SCHEMAV_ATTRUNKNOWN, XML_SCHEMAV_ATTRINVALID, XML_SCHEMAV_VALUE, XML_SCHEMAV_FACET, XML_SCHEMAV_CVC_DATATYPE_VALID_1_2_1, XML_SCHEMAV_CVC_DATATYPE_VALID_1_2_2, XML_SCHEMAV_CVC_DATATYPE_VALID_1_2_3, XML_SCHEMAV_CVC_TYPE_3_1_1, XML_SCHEMAV_CVC_TYPE_3_1_2, XML_SCHEMAV_CVC_FACET_VALID, XML_SCHEMAV_CVC_LENGTH_VALID, XML_SCHEMAV_CVC_MINLENGTH_VALID, XML_SCHEMAV_CVC_MAXLENGTH_VALID, XML_SCHEMAV_CVC_MININCLUSIVE_VALID, XML_SCHEMAV_CVC_MAXINCLUSIVE_VALID, XML_SCHEMAV_CVC_MINEXCLUSIVE_VALID, XML_SCHEMAV_CVC_MAXEXCLUSIVE_VALID, XML_SCHEMAV_CVC_TOTALDIGITS_VALID, XML_SCHEMAV_CVC_FRACTIONDIGITS_VALID, XML_SCHEMAV_CVC_PATTERN_VALID, XML_SCHEMAV_CVC_ENUMERATION_VALID, XML_SCHEMAV_CVC_COMPLEX_TYPE_2_1, XML_SCHEMAV_CVC_COMPLEX_TYPE_2_2, XML_SCHEMAV_CVC_COMPLEX_TYPE_2_3, XML_SCHEMAV_CVC_COMPLEX_TYPE_2_4, XML_SCHEMAV_CVC_ELT_1, XML_SCHEMAV_CVC_ELT_2, XML_SCHEMAV_CVC_ELT_3_1, XML_SCHEMAV_CVC_ELT_3_2_1, XML_SCHEMAV_CVC_ELT_3_2_2, XML_SCHEMAV_CVC_ELT_4_1, XML_SCHEMAV_CVC_ELT_4_2, XML_SCHEMAV_CVC_ELT_4_3, XML_SCHEMAV_CVC_ELT_5_1_1, XML_SCHEMAV_CVC_ELT_5_1_2, XML_SCHEMAV_CVC_ELT_5_2_1, XML_SCHEMAV_CVC_ELT_5_2_2_1, XML_SCHEMAV_CVC_ELT_5_2_2_2_1, XML_SCHEMAV_CVC_ELT_5_2_2_2_2, XML_SCHEMAV_CVC_ELT_6, XML_SCHEMAV_CVC_ELT_7, XML_SCHEMAV_CVC_ATTRIBUTE_1, XML_SCHEMAV_CVC_ATTRIBUTE_2, XML_SCHEMAV_CVC_ATTRIBUTE_3, XML_SCHEMAV_CVC_ATTRIBUTE_4, XML_SCHEMAV_CVC_COMPLEX_TYPE_3_1, XML_SCHEMAV_CVC_COMPLEX_TYPE_3_2_1, XML_SCHEMAV_CVC_COMPLEX_TYPE_3_2_2, XML_SCHEMAV_CVC_COMPLEX_TYPE_4, XML_SCHEMAV_CVC_COMPLEX_TYPE_5_1, XML_SCHEMAV_CVC_COMPLEX_TYPE_5_2, XML_SCHEMAV_ELEMENT_CONTENT, XML_SCHEMAV_DOCUMENT_ELEMENT_MISSING, XML_SCHEMAV_CVC_COMPLEX_TYPE_1, XML_SCHEMAV_CVC_AU, XML_SCHEMAV_CVC_TYPE_1, XML_SCHEMAV_CVC_TYPE_2, XML_SCHEMAV_CVC_IDC, XML_SCHEMAV_CVC_WILDCARD, XML_SCHEMAV_MISC, XML_XPTR_UNKNOWN_SCHEME = 1900, XML_XPTR_CHILDSEQ_START, XML_XPTR_EVAL_FAILED, XML_XPTR_EXTRA_OBJECTS, XML_C14N_CREATE_CTXT = 1950, XML_C14N_REQUIRES_UTF8, XML_C14N_CREATE_STACK, XML_C14N_INVALID_NODE, XML_C14N_UNKNOW_NODE, XML_C14N_RELATIVE_NAMESPACE, XML_FTP_PASV_ANSWER = 2000, XML_FTP_EPSV_ANSWER, XML_FTP_ACCNT, XML_FTP_URL_SYNTAX, XML_HTTP_URL_SYNTAX = 2020, XML_HTTP_USE_IP, XML_HTTP_UNKNOWN_HOST, XML_SCHEMAP_SRC_SIMPLE_TYPE_1 = 3000, XML_SCHEMAP_SRC_SIMPLE_TYPE_2, XML_SCHEMAP_SRC_SIMPLE_TYPE_3, XML_SCHEMAP_SRC_SIMPLE_TYPE_4, XML_SCHEMAP_SRC_RESOLVE, XML_SCHEMAP_SRC_RESTRICTION_BASE_OR_SIMPLETYPE, XML_SCHEMAP_SRC_LIST_ITEMTYPE_OR_SIMPLETYPE, XML_SCHEMAP_SRC_UNION_MEMBERTYPES_OR_SIMPLETYPES, XML_SCHEMAP_ST_PROPS_CORRECT_1, XML_SCHEMAP_ST_PROPS_CORRECT_2, XML_SCHEMAP_ST_PROPS_CORRECT_3, XML_SCHEMAP_COS_ST_RESTRICTS_1_1, XML_SCHEMAP_COS_ST_RESTRICTS_1_2, XML_SCHEMAP_COS_ST_RESTRICTS_1_3_1, XML_SCHEMAP_COS_ST_RESTRICTS_1_3_2, XML_SCHEMAP_COS_ST_RESTRICTS_2_1, XML_SCHEMAP_COS_ST_RESTRICTS_2_3_1_1, XML_SCHEMAP_COS_ST_RESTRICTS_2_3_1_2, XML_SCHEMAP_COS_ST_RESTRICTS_2_3_2_1, XML_SCHEMAP_COS_ST_RESTRICTS_2_3_2_2, XML_SCHEMAP_COS_ST_RESTRICTS_2_3_2_3, XML_SCHEMAP_COS_ST_RESTRICTS_2_3_2_4, XML_SCHEMAP_COS_ST_RESTRICTS_2_3_2_5, XML_SCHEMAP_COS_ST_RESTRICTS_3_1, XML_SCHEMAP_COS_ST_RESTRICTS_3_3_1, XML_SCHEMAP_COS_ST_RESTRICTS_3_3_1_2, XML_SCHEMAP_COS_ST_RESTRICTS_3_3_2_2, XML_SCHEMAP_COS_ST_RESTRICTS_3_3_2_1, XML_SCHEMAP_COS_ST_RESTRICTS_3_3_2_3, XML_SCHEMAP_COS_ST_RESTRICTS_3_3_2_4, XML_SCHEMAP_COS_ST_RESTRICTS_3_3_2_5, XML_SCHEMAP_COS_ST_DERIVED_OK_2_1, XML_SCHEMAP_COS_ST_DERIVED_OK_2_2, XML_SCHEMAP_S4S_ELEM_NOT_ALLOWED, XML_SCHEMAP_S4S_ELEM_MISSING, XML_SCHEMAP_S4S_ATTR_NOT_ALLOWED, XML_SCHEMAP_S4S_ATTR_MISSING, XML_SCHEMAP_S4S_ATTR_INVALID_VALUE, XML_SCHEMAP_SRC_ELEMENT_1, XML_SCHEMAP_SRC_ELEMENT_2_1, XML_SCHEMAP_SRC_ELEMENT_2_2, XML_SCHEMAP_SRC_ELEMENT_3, XML_SCHEMAP_P_PROPS_CORRECT_1, XML_SCHEMAP_P_PROPS_CORRECT_2_1, XML_SCHEMAP_P_PROPS_CORRECT_2_2, XML_SCHEMAP_E_PROPS_CORRECT_2, XML_SCHEMAP_E_PROPS_CORRECT_3, XML_SCHEMAP_E_PROPS_CORRECT_4, XML_SCHEMAP_E_PROPS_CORRECT_5, XML_SCHEMAP_E_PROPS_CORRECT_6, XML_SCHEMAP_SRC_INCLUDE, XML_SCHEMAP_SRC_ATTRIBUTE_1, XML_SCHEMAP_SRC_ATTRIBUTE_2, XML_SCHEMAP_SRC_ATTRIBUTE_3_1, XML_SCHEMAP_SRC_ATTRIBUTE_3_2, XML_SCHEMAP_SRC_ATTRIBUTE_4, XML_SCHEMAP_NO_XMLNS, XML_SCHEMAP_NO_XSI, XML_SCHEMAP_COS_VALID_DEFAULT_1, XML_SCHEMAP_COS_VALID_DEFAULT_2_1, XML_SCHEMAP_COS_VALID_DEFAULT_2_2_1, XML_SCHEMAP_COS_VALID_DEFAULT_2_2_2, XML_SCHEMAP_CVC_SIMPLE_TYPE, XML_SCHEMAP_COS_CT_EXTENDS_1_1, XML_SCHEMAP_SRC_IMPORT_1_1, XML_SCHEMAP_SRC_IMPORT_1_2, XML_SCHEMAP_SRC_IMPORT_2, XML_SCHEMAP_SRC_IMPORT_2_1, XML_SCHEMAP_SRC_IMPORT_2_2, XML_SCHEMAP_INTERNAL, XML_SCHEMAP_NOT_DETERMINISTIC, XML_SCHEMAP_SRC_ATTRIBUTE_GROUP_1, XML_SCHEMAP_SRC_ATTRIBUTE_GROUP_2, XML_SCHEMAP_SRC_ATTRIBUTE_GROUP_3, XML_SCHEMAP_MG_PROPS_CORRECT_1, XML_SCHEMAP_MG_PROPS_CORRECT_2, XML_SCHEMAP_SRC_CT_1, XML_SCHEMAP_DERIVATION_OK_RESTRICTION_2_1_3, XML_SCHEMAP_AU_PROPS_CORRECT_2, XML_SCHEMAP_A_PROPS_CORRECT_2, XML_SCHEMAP_C_PROPS_CORRECT, XML_SCHEMAP_SRC_REDEFINE, XML_SCHEMAP_SRC_IMPORT, XML_SCHEMAP_WARN_SKIP_SCHEMA, XML_SCHEMAP_WARN_UNLOCATED_SCHEMA, XML_SCHEMAP_WARN_ATTR_REDECL_PROH, XML_SCHEMAP_WARN_ATTR_POINTLESS_PROH, XML_SCHEMAP_AG_PROPS_CORRECT, XML_SCHEMAP_COS_CT_EXTENDS_1_2, XML_SCHEMAP_AU_PROPS_CORRECT, XML_SCHEMAP_A_PROPS_CORRECT_3, XML_SCHEMAP_COS_ALL_LIMITED, XML_SCHEMATRONV_ASSERT = 4000, XML_SCHEMATRONV_REPORT, XML_MODULE_OPEN = 4900, XML_MODULE_CLOSE, XML_CHECK_FOUND_ELEMENT = 5000, XML_CHECK_FOUND_ATTRIBUTE, XML_CHECK_FOUND_TEXT, XML_CHECK_FOUND_CDATA, XML_CHECK_FOUND_ENTITYREF, XML_CHECK_FOUND_ENTITY, XML_CHECK_FOUND_PI, XML_CHECK_FOUND_COMMENT, XML_CHECK_FOUND_DOCTYPE, XML_CHECK_FOUND_FRAGMENT, XML_CHECK_FOUND_NOTATION, XML_CHECK_UNKNOWN_NODE, XML_CHECK_ENTITY_TYPE, XML_CHECK_NO_PARENT, XML_CHECK_NO_DOC, XML_CHECK_NO_NAME, XML_CHECK_NO_ELEM, XML_CHECK_WRONG_DOC, XML_CHECK_NO_PREV, XML_CHECK_WRONG_PREV, XML_CHECK_NO_NEXT, XML_CHECK_WRONG_NEXT, XML_CHECK_NOT_DTD, XML_CHECK_NOT_ATTR, XML_CHECK_NOT_ATTR_DECL, XML_CHECK_NOT_ELEM_DECL, XML_CHECK_NOT_ENTITY_DECL, XML_CHECK_NOT_NS_DECL, XML_CHECK_NO_HREF, XML_CHECK_WRONG_PARENT, XML_CHECK_NS_SCOPE, XML_CHECK_NS_ANCESTOR, XML_CHECK_NOT_UTF8, XML_CHECK_NO_DICT, XML_CHECK_NOT_NCNAME, XML_CHECK_OUTSIDE_DICT, XML_CHECK_WRONG_NAME, XML_CHECK_NAME_NOT_NULL, XML_I18N_NO_NAME = 6000, XML_I18N_NO_HANDLER, XML_I18N_EXCESS_HANDLER, XML_I18N_CONV_FAILED, XML_I18N_NO_OUTPUT, XML_BUF_OVERFLOW = 7000 } xmlParserErrors;
typedef void (*xmlGenericErrorFunc) (void *ctx, const char *msg, ...) __attribute__((__format__(__printf__,2,3)));
typedef void (*xmlStructuredErrorFunc) (void *userData, const xmlError *error);
struct _xmlParserInput;
typedef struct _xmlLink xmlLink;
typedef xmlLink *xmlLinkPtr;
typedef struct _xmlList xmlList;
typedef xmlList *xmlListPtr;
typedef void (*xmlListDeallocator) (xmlLink *lk);
typedef int (*xmlListDataCompare) (const void *data0, const void *data1);
typedef int (*xmlListWalker) (const void *data, void *user);
typedef struct _xmlAutomata xmlAutomata;
typedef xmlAutomata *xmlAutomataPtr;
typedef struct _xmlAutomataState xmlAutomataState;
typedef xmlAutomataState *xmlAutomataStatePtr;
typedef struct _xmlValidState xmlValidState;
typedef xmlValidState *xmlValidStatePtr;
typedef void (*xmlValidityErrorFunc) (void *ctx, const char *msg, ...) __attribute__((__format__(__printf__,2,3)));
typedef void (*xmlValidityWarningFunc) (void *ctx, const char *msg, ...) __attribute__((__format__(__printf__,2,3)));
typedef struct _xmlValidCtxt xmlValidCtxt;
typedef xmlValidCtxt *xmlValidCtxtPtr;
struct _xmlValidCtxt { void *userData; xmlValidityErrorFunc error; xmlValidityWarningFunc warning; xmlNode *node; int nodeNr; int nodeMax; xmlNode **nodeTab; unsigned int flags; xmlDoc *doc; int valid; xmlValidState *vstate; int vstateNr; int vstateMax; xmlValidState *vstateTab; xmlAutomata *am; xmlAutomataState *state; };
typedef struct _xmlHashTable xmlNotationTable;
typedef xmlNotationTable *xmlNotationTablePtr;
typedef struct _xmlHashTable xmlElementTable;
typedef xmlElementTable *xmlElementTablePtr;
typedef struct _xmlHashTable xmlAttributeTable;
typedef xmlAttributeTable *xmlAttributeTablePtr;
typedef struct _xmlHashTable xmlIDTable;
typedef xmlIDTable *xmlIDTablePtr;
typedef struct _xmlHashTable xmlRefTable;
typedef xmlRefTable *xmlRefTablePtr;
typedef enum { XML_INTERNAL_GENERAL_ENTITY = 1, XML_EXTERNAL_GENERAL_PARSED_ENTITY = 2, XML_EXTERNAL_GENERAL_UNPARSED_ENTITY = 3, XML_INTERNAL_PARAMETER_ENTITY = 4, XML_EXTERNAL_PARAMETER_ENTITY = 5, XML_INTERNAL_PREDEFINED_ENTITY = 6 } xmlEntityType;
struct _xmlEntity { void *_private; xmlElementType type; const xmlChar *name; struct _xmlNode *children; struct _xmlNode *last; struct _xmlDtd *parent; struct _xmlNode *next; struct _xmlNode *prev; struct _xmlDoc *doc; xmlChar *orig; xmlChar *content; int length; xmlEntityType etype; const xmlChar *ExternalID; const xmlChar *SystemID; struct _xmlEntity *nexte; const xmlChar *URI; int owner; int flags; unsigned long expandedSize; };
typedef struct _xmlHashTable xmlEntitiesTable;
typedef xmlEntitiesTable *xmlEntitiesTablePtr;
typedef enum { XML_ENC_ERR_SUCCESS = 0, XML_ENC_ERR_INTERNAL = -1, XML_ENC_ERR_INPUT = -2, XML_ENC_ERR_SPACE = -3, XML_ENC_ERR_MEMORY = -4 } xmlCharEncError;
typedef enum { XML_CHAR_ENCODING_ERROR= -1, XML_CHAR_ENCODING_NONE= 0, XML_CHAR_ENCODING_UTF8= 1, XML_CHAR_ENCODING_UTF16LE= 2, XML_CHAR_ENCODING_UTF16BE= 3, XML_CHAR_ENCODING_UCS4LE= 4, XML_CHAR_ENCODING_UCS4BE= 5, XML_CHAR_ENCODING_EBCDIC= 6, XML_CHAR_ENCODING_UCS4_2143=7, XML_CHAR_ENCODING_UCS4_3412=8, XML_CHAR_ENCODING_UCS2= 9, XML_CHAR_ENCODING_8859_1= 10, XML_CHAR_ENCODING_8859_2= 11, XML_CHAR_ENCODING_8859_3= 12, XML_CHAR_ENCODING_8859_4= 13, XML_CHAR_ENCODING_8859_5= 14, XML_CHAR_ENCODING_8859_6= 15, XML_CHAR_ENCODING_8859_7= 16, XML_CHAR_ENCODING_8859_8= 17, XML_CHAR_ENCODING_8859_9= 18, XML_CHAR_ENCODING_2022_JP= 19, XML_CHAR_ENCODING_SHIFT_JIS=20, XML_CHAR_ENCODING_EUC_JP= 21, XML_CHAR_ENCODING_ASCII= 22, XML_CHAR_ENCODING_UTF16= 23, XML_CHAR_ENCODING_HTML= 24, XML_CHAR_ENCODING_8859_10= 25, XML_CHAR_ENCODING_8859_11= 26, XML_CHAR_ENCODING_8859_13= 27, XML_CHAR_ENCODING_8859_14= 28, XML_CHAR_ENCODING_8859_15= 29, XML_CHAR_ENCODING_8859_16= 30, XML_CHAR_ENCODING_WINDOWS_1252 = 31 } xmlCharEncoding;
typedef enum { XML_ENC_INPUT = (1 << 0), XML_ENC_OUTPUT = (1 << 1), XML_ENC_HTML = (1 << 2) } xmlCharEncFlags;
typedef int (*xmlCharEncodingInputFunc)(unsigned char *out, int *outlen, const unsigned char *in, int *inlen);
typedef int (*xmlCharEncodingOutputFunc)(unsigned char *out, int *outlen, const unsigned char *in, int *inlen);
typedef xmlCharEncError (*xmlCharEncConvFunc)(void *vctxt, unsigned char *out, int *outlen, const unsigned char *in, int *inlen, int flush);
typedef void (*xmlCharEncConvCtxtDtor)(void *vctxt);
typedef struct _xmlCharEncodingHandler xmlCharEncodingHandler;
typedef xmlCharEncodingHandler *xmlCharEncodingHandlerPtr;
struct _xmlCharEncodingHandler { char *name __attribute__((deprecated)); union { xmlCharEncConvFunc func; xmlCharEncodingInputFunc legacyFunc; } input __attribute__((deprecated)); union { xmlCharEncConvFunc func; xmlCharEncodingOutputFunc legacyFunc; } output __attribute__((deprecated)); void *inputCtxt __attribute__((deprecated)); void *outputCtxt __attribute__((deprecated)); xmlCharEncConvCtxtDtor ctxtDtor __attribute__((deprecated)); int flags __attribute__((deprecated)); };
typedef xmlParserErrors (*xmlCharEncConvImpl)(void *vctxt, const char *name, xmlCharEncFlags flags, xmlCharEncodingHandler **out);
struct _xmlBuffer;
typedef int (*xmlInputMatchCallback) (const char *filename);
typedef void * (*xmlInputOpenCallback) (const char *filename);
typedef int (*xmlInputReadCallback) (void * context, char * buffer, int len);
typedef int (*xmlInputCloseCallback) (void * context);
typedef int (*xmlOutputMatchCallback) (const char *filename);
typedef void * (*xmlOutputOpenCallback) (const char *filename);
typedef int (*xmlOutputWriteCallback) (void * context, const char * buffer, int len);
typedef int (*xmlOutputCloseCallback) (void * context);
typedef xmlParserInputBuffer * (*xmlParserInputBufferCreateFilenameFunc)(const char *URI, xmlCharEncoding enc);
typedef xmlOutputBuffer * (*xmlOutputBufferCreateFilenameFunc)(const char *URI, xmlCharEncodingHandler *encoder, int compression);
struct _xmlParserInputBuffer { void* context __attribute__((deprecated)); xmlInputReadCallback readcallback __attribute__((deprecated)); xmlInputCloseCallback closecallback __attribute__((deprecated)); xmlCharEncodingHandler *encoder __attribute__((deprecated)); xmlBuf *buffer __attribute__((deprecated)); xmlBuf *raw __attribute__((deprecated)); int compressed __attribute__((deprecated)); int error __attribute__((deprecated)); unsigned long rawconsumed __attribute__((deprecated)); };
struct _xmlOutputBuffer { void* context; xmlOutputWriteCallback writecallback; xmlOutputCloseCallback closecallback; xmlCharEncodingHandler *encoder; xmlBuf *buffer; xmlBuf *conv; int written; int error; };
typedef struct _xmlMutex xmlMutex;
typedef xmlMutex *xmlMutexPtr;
typedef struct _xmlRMutex xmlRMutex;
typedef xmlRMutex *xmlRMutexPtr;
typedef enum { XML_STATUS_NOT_WELL_FORMED = (1 << 0), XML_STATUS_NOT_NS_WELL_FORMED = (1 << 1), XML_STATUS_DTD_VALIDATION_FAILED = (1 << 2), XML_STATUS_CATASTROPHIC_ERROR = (1 << 3) } xmlParserStatus;
typedef enum { XML_RESOURCE_UNKNOWN = 0, XML_RESOURCE_MAIN_DOCUMENT, XML_RESOURCE_DTD, XML_RESOURCE_GENERAL_ENTITY, XML_RESOURCE_PARAMETER_ENTITY, XML_RESOURCE_XINCLUDE, XML_RESOURCE_XINCLUDE_TEXT } xmlResourceType;
typedef enum { XML_INPUT_BUF_STATIC = (1 << 1), XML_INPUT_BUF_ZERO_TERMINATED = (1 << 2), XML_INPUT_UNZIP = (1 << 3), XML_INPUT_NETWORK = (1 << 4), XML_INPUT_USE_SYS_CATALOG = (1 << 5) } xmlParserInputFlags;
typedef void (* xmlParserInputDeallocate)(xmlChar *str);
struct _xmlParserInput { xmlParserInputBuffer *buf __attribute__((deprecated)); const char *filename; const char *directory __attribute__((deprecated)); const xmlChar *base; const xmlChar *cur; const xmlChar *end; int length __attribute__((deprecated)); int line; int col; unsigned long consumed; xmlParserInputDeallocate free __attribute__((deprecated)); const xmlChar *encoding __attribute__((deprecated)); const xmlChar *version __attribute__((deprecated)); int flags __attribute__((deprecated)); int id __attribute__((deprecated)); unsigned long parentConsumed __attribute__((deprecated)); xmlEntity *entity __attribute__((deprecated)); };
typedef struct _xmlParserNodeInfo xmlParserNodeInfo;
typedef xmlParserNodeInfo *xmlParserNodeInfoPtr;
struct _xmlParserNodeInfo { const struct _xmlNode* node; unsigned long begin_pos; unsigned long begin_line; unsigned long end_pos; unsigned long end_line; };
typedef struct _xmlParserNodeInfoSeq xmlParserNodeInfoSeq;
typedef xmlParserNodeInfoSeq *xmlParserNodeInfoSeqPtr;
struct _xmlParserNodeInfoSeq { unsigned long maximum; unsigned long length; xmlParserNodeInfo* buffer; };
typedef enum { XML_PARSER_EOF = -1, XML_PARSER_START = 0, XML_PARSER_MISC, XML_PARSER_PI, XML_PARSER_DTD, XML_PARSER_PROLOG, XML_PARSER_COMMENT, XML_PARSER_START_TAG, XML_PARSER_CONTENT, XML_PARSER_CDATA_SECTION, XML_PARSER_END_TAG, XML_PARSER_ENTITY_DECL, XML_PARSER_ENTITY_VALUE, XML_PARSER_ATTRIBUTE_VALUE, XML_PARSER_SYSTEM_LITERAL, XML_PARSER_EPILOG, XML_PARSER_IGNORE, XML_PARSER_PUBLIC_LITERAL, XML_PARSER_XML_DECL } xmlParserInputState;
typedef enum { XML_PARSE_UNKNOWN = 0, XML_PARSE_DOM = 1, XML_PARSE_SAX = 2, XML_PARSE_PUSH_DOM = 3, XML_PARSE_PUSH_SAX = 4, XML_PARSE_READER = 5 } xmlParserMode;
typedef struct _xmlStartTag xmlStartTag;
typedef struct _xmlParserNsData xmlParserNsData;
typedef struct _xmlAttrHashBucket xmlAttrHashBucket;
typedef xmlParserErrors (*xmlResourceLoader)(void *ctxt, const char *url, const char *publicId, xmlResourceType type, xmlParserInputFlags flags, xmlParserInput **out);
struct _xmlParserCtxt { struct _xmlSAXHandler *sax; void *userData; xmlDoc *myDoc; int wellFormed; int replaceEntities __attribute__((deprecated)); xmlChar *version; xmlChar *encoding; int standalone; int html; xmlParserInput *input; int inputNr; int inputMax __attribute__((deprecated)); xmlParserInput **inputTab; xmlNode *node; int nodeNr __attribute__((deprecated)); int nodeMax __attribute__((deprecated)); xmlNode **nodeTab __attribute__((deprecated)); int record_info; xmlParserNodeInfoSeq node_seq __attribute__((deprecated)); int errNo; int hasExternalSubset __attribute__((deprecated)); int hasPErefs __attribute__((deprecated)); int external __attribute__((deprecated)); int valid; int validate __attribute__((deprecated)); xmlValidCtxt vctxt; xmlParserInputState instate __attribute__((deprecated)); int token __attribute__((deprecated)); char *directory; const xmlChar *name __attribute__((deprecated)); int nameNr __attribute__((deprecated)); int nameMax __attribute__((deprecated)); const xmlChar **nameTab __attribute__((deprecated)); long nbChars __attribute__((deprecated)); long checkIndex __attribute__((deprecated)); int keepBlanks __attribute__((deprecated)); int disableSAX __attribute__((deprecated)); int inSubset; const xmlChar *intSubName; xmlChar *extSubURI; xmlChar *extSubSystem; int *space __attribute__((deprecated)); int spaceNr __attribute__((deprecated)); int spaceMax __attribute__((deprecated)); int *spaceTab __attribute__((deprecated)); int depth __attribute__((deprecated)); xmlParserInput *entity __attribute__((deprecated)); int charset __attribute__((deprecated)); int nodelen __attribute__((deprecated)); int nodemem __attribute__((deprecated)); int pedantic __attribute__((deprecated)); void *_private; int loadsubset __attribute__((deprecated)); int linenumbers __attribute__((deprecated)); void *catalogs __attribute__((deprecated)); int recovery __attribute__((deprecated)); int progressive __attribute__((deprecated)); xmlDict *dict; const xmlChar **atts __attribute__((deprecated)); int maxatts __attribute__((deprecated)); int docdict __attribute__((deprecated)); const xmlChar *str_xml __attribute__((deprecated)); const xmlChar *str_xmlns __attribute__((deprecated)); const xmlChar *str_xml_ns __attribute__((deprecated)); int sax2 __attribute__((deprecated)); int nsNr __attribute__((deprecated)); int nsMax __attribute__((deprecated)); const xmlChar **nsTab __attribute__((deprecated)); unsigned *attallocs __attribute__((deprecated)); xmlStartTag *pushTab __attribute__((deprecated)); xmlHashTable *attsDefault __attribute__((deprecated)); xmlHashTable *attsSpecial __attribute__((deprecated)); int nsWellFormed; int options; int dictNames __attribute__((deprecated)); int freeElemsNr __attribute__((deprecated)); xmlNode *freeElems __attribute__((deprecated)); int freeAttrsNr __attribute__((deprecated)); xmlAttr *freeAttrs __attribute__((deprecated)); xmlError lastError __attribute__((deprecated)); xmlParserMode parseMode __attribute__((deprecated)); unsigned long nbentities __attribute__((deprecated)); unsigned long sizeentities __attribute__((deprecated)); xmlParserNodeInfo *nodeInfo __attribute__((deprecated)); int nodeInfoNr __attribute__((deprecated)); int nodeInfoMax __attribute__((deprecated)); xmlParserNodeInfo *nodeInfoTab __attribute__((deprecated)); int input_id __attribute__((deprecated)); unsigned long sizeentcopy __attribute__((deprecated)); int endCheckState __attribute__((deprecated)); unsigned short nbErrors __attribute__((deprecated)); unsigned short nbWarnings __attribute__((deprecated)); unsigned maxAmpl __attribute__((deprecated)); xmlParserNsData *nsdb __attribute__((deprecated)); unsigned attrHashMax __attribute__((deprecated)); xmlAttrHashBucket *attrHash __attribute__((deprecated)); xmlStructuredErrorFunc errorHandler __attribute__((deprecated)); void *errorCtxt __attribute__((deprecated)); xmlResourceLoader resourceLoader __attribute__((deprecated)); void *resourceCtxt __attribute__((deprecated)); xmlCharEncConvImpl convImpl __attribute__((deprecated)); void *convCtxt __attribute__((deprecated)); };
struct _xmlSAXLocator { const xmlChar *(*getPublicId)(void *ctx); const xmlChar *(*getSystemId)(void *ctx); int (*getLineNumber)(void *ctx); int (*getColumnNumber)(void *ctx); };
typedef xmlParserInput *(*resolveEntitySAXFunc) (void *ctx, const xmlChar *publicId, const xmlChar *systemId);
typedef void (*internalSubsetSAXFunc) (void *ctx, const xmlChar *name, const xmlChar *publicId, const xmlChar *systemId);
typedef void (*externalSubsetSAXFunc) (void *ctx, const xmlChar *name, const xmlChar *publicId, const xmlChar *systemId);
typedef xmlEntity *(*getEntitySAXFunc) (void *ctx, const xmlChar *name);
typedef xmlEntity *(*getParameterEntitySAXFunc) (void *ctx, const xmlChar *name);
typedef void (*entityDeclSAXFunc) (void *ctx, const xmlChar *name, int type, const xmlChar *publicId, const xmlChar *systemId, xmlChar *content);
typedef void (*notationDeclSAXFunc)(void *ctx, const xmlChar *name, const xmlChar *publicId, const xmlChar *systemId);
typedef void (*attributeDeclSAXFunc)(void *ctx, const xmlChar *elem, const xmlChar *fullname, int type, int def, const xmlChar *defaultValue, xmlEnumeration *tree);
typedef void (*elementDeclSAXFunc)(void *ctx, const xmlChar *name, int type, xmlElementContent *content);
typedef void (*unparsedEntityDeclSAXFunc)(void *ctx, const xmlChar *name, const xmlChar *publicId, const xmlChar *systemId, const xmlChar *notationName);
typedef void (*setDocumentLocatorSAXFunc) (void *ctx, xmlSAXLocator *loc);
typedef void (*startDocumentSAXFunc) (void *ctx);
typedef void (*endDocumentSAXFunc) (void *ctx);
typedef void (*startElementSAXFunc) (void *ctx, const xmlChar *name, const xmlChar **atts);
typedef void (*endElementSAXFunc) (void *ctx, const xmlChar *name);
typedef void (*attributeSAXFunc) (void *ctx, const xmlChar *name, const xmlChar *value);
typedef void (*referenceSAXFunc) (void *ctx, const xmlChar *name);
typedef void (*charactersSAXFunc) (void *ctx, const xmlChar *ch, int len);
typedef void (*ignorableWhitespaceSAXFunc) (void *ctx, const xmlChar *ch, int len);
typedef void (*processingInstructionSAXFunc) (void *ctx, const xmlChar *target, const xmlChar *data);
typedef void (*commentSAXFunc) (void *ctx, const xmlChar *value);
typedef void (*cdataBlockSAXFunc) ( void *ctx, const xmlChar *value, int len);
typedef void (*warningSAXFunc) (void *ctx, const char *msg, ...) __attribute__((__format__(__printf__,2,3)));
typedef void (*errorSAXFunc) (void *ctx, const char *msg, ...) __attribute__((__format__(__printf__,2,3)));
typedef void (*fatalErrorSAXFunc) (void *ctx, const char *msg, ...) __attribute__((__format__(__printf__,2,3)));
typedef int (*isStandaloneSAXFunc) (void *ctx);
typedef int (*hasInternalSubsetSAXFunc) (void *ctx);
typedef int (*hasExternalSubsetSAXFunc) (void *ctx);
typedef void (*startElementNsSAX2Func) (void *ctx, const xmlChar *localname, const xmlChar *prefix, const xmlChar *URI, int nb_namespaces, const xmlChar **namespaces, int nb_attributes, int nb_defaulted, const xmlChar **attributes);
typedef void (*endElementNsSAX2Func) (void *ctx, const xmlChar *localname, const xmlChar *prefix, const xmlChar *URI);
struct _xmlSAXHandler { internalSubsetSAXFunc internalSubset; isStandaloneSAXFunc isStandalone; hasInternalSubsetSAXFunc hasInternalSubset; hasExternalSubsetSAXFunc hasExternalSubset; resolveEntitySAXFunc resolveEntity; getEntitySAXFunc getEntity; entityDeclSAXFunc entityDecl; notationDeclSAXFunc notationDecl; attributeDeclSAXFunc attributeDecl; elementDeclSAXFunc elementDecl; unparsedEntityDeclSAXFunc unparsedEntityDecl; setDocumentLocatorSAXFunc setDocumentLocator; startDocumentSAXFunc startDocument; endDocumentSAXFunc endDocument; startElementSAXFunc startElement; endElementSAXFunc endElement; referenceSAXFunc reference; charactersSAXFunc characters; ignorableWhitespaceSAXFunc ignorableWhitespace; processingInstructionSAXFunc processingInstruction; commentSAXFunc comment; warningSAXFunc warning; errorSAXFunc error; fatalErrorSAXFunc fatalError; getParameterEntitySAXFunc getParameterEntity; cdataBlockSAXFunc cdataBlock; externalSubsetSAXFunc externalSubset; unsigned int initialized; void *_private; startElementNsSAX2Func startElementNs; endElementNsSAX2Func endElementNs; xmlStructuredErrorFunc serror; };
typedef struct _xmlSAXHandlerV1 xmlSAXHandlerV1;
typedef xmlSAXHandlerV1 *xmlSAXHandlerV1Ptr;
struct _xmlSAXHandlerV1 { internalSubsetSAXFunc internalSubset; isStandaloneSAXFunc isStandalone; hasInternalSubsetSAXFunc hasInternalSubset; hasExternalSubsetSAXFunc hasExternalSubset; resolveEntitySAXFunc resolveEntity; getEntitySAXFunc getEntity; entityDeclSAXFunc entityDecl; notationDeclSAXFunc notationDecl; attributeDeclSAXFunc attributeDecl; elementDeclSAXFunc elementDecl; unparsedEntityDeclSAXFunc unparsedEntityDecl; setDocumentLocatorSAXFunc setDocumentLocator; startDocumentSAXFunc startDocument; endDocumentSAXFunc endDocument; startElementSAXFunc startElement; endElementSAXFunc endElement; referenceSAXFunc reference; charactersSAXFunc characters; ignorableWhitespaceSAXFunc ignorableWhitespace; processingInstructionSAXFunc processingInstruction; commentSAXFunc comment; warningSAXFunc warning; errorSAXFunc error; fatalErrorSAXFunc fatalError; getParameterEntitySAXFunc getParameterEntity; cdataBlockSAXFunc cdataBlock; externalSubsetSAXFunc externalSubset; unsigned int initialized; };
typedef xmlParserInput *(*xmlExternalEntityLoader) (const char *URL, const char *publicId, xmlParserCtxt *context);
typedef enum { XML_PARSE_RECOVER = 1<<0, XML_PARSE_NOENT = 1<<1, XML_PARSE_DTDLOAD = 1<<2, XML_PARSE_DTDATTR = 1<<3, XML_PARSE_DTDVALID = 1<<4, XML_PARSE_NOERROR = 1<<5, XML_PARSE_NOWARNING = 1<<6, XML_PARSE_PEDANTIC = 1<<7, XML_PARSE_NOBLANKS = 1<<8, XML_PARSE_SAX1 = 1<<9, XML_PARSE_XINCLUDE = 1<<10, XML_PARSE_NONET = 1<<11, XML_PARSE_NODICT = 1<<12, XML_PARSE_NSCLEAN = 1<<13, XML_PARSE_NOCDATA = 1<<14, XML_PARSE_NOXINCNODE = 1<<15, XML_PARSE_COMPACT = 1<<16, XML_PARSE_OLD10 = 1<<17, XML_PARSE_NOBASEFIX = 1<<18, XML_PARSE_HUGE = 1<<19, XML_PARSE_OLDSAX = 1<<20, XML_PARSE_IGNORE_ENC = 1<<21, XML_PARSE_BIG_LINES = 1<<22, XML_PARSE_NO_XXE = 1<<23, XML_PARSE_UNZIP = 1<<24, XML_PARSE_NO_SYS_CATALOG = 1<<25, XML_PARSE_CATALOG_PI = 1<<26, XML_PARSE_SKIP_IDS = 1<<27 } xmlParserOption;
typedef enum { XML_WITH_THREAD = 1, XML_WITH_TREE = 2, XML_WITH_OUTPUT = 3, XML_WITH_PUSH = 4, XML_WITH_READER = 5, XML_WITH_PATTERN = 6, XML_WITH_WRITER = 7, XML_WITH_SAX1 = 8, XML_WITH_FTP = 9, XML_WITH_HTTP = 10, XML_WITH_VALID = 11, XML_WITH_HTML = 12, XML_WITH_LEGACY = 13, XML_WITH_C14N = 14, XML_WITH_CATALOG = 15, XML_WITH_XPATH = 16, XML_WITH_XPTR = 17, XML_WITH_XINCLUDE = 18, XML_WITH_ICONV = 19, XML_WITH_ISO8859X = 20, XML_WITH_UNICODE = 21, XML_WITH_REGEXP = 22, XML_WITH_AUTOMATA = 23, XML_WITH_EXPR = 24, XML_WITH_SCHEMAS = 25, XML_WITH_SCHEMATRON = 26, XML_WITH_MODULES = 27, XML_WITH_DEBUG = 28, XML_WITH_DEBUG_MEM = 29, XML_WITH_DEBUG_RUN = 30, XML_WITH_ZLIB = 31, XML_WITH_ICU = 32, XML_WITH_LZMA = 33, XML_WITH_RELAXNG = 34, XML_WITH_NONE = 99999 } xmlFeature;
typedef struct _xmlXPathContext xmlXPathContext;
typedef xmlXPathContext *xmlXPathContextPtr;
typedef struct _xmlXPathParserContext xmlXPathParserContext;
typedef xmlXPathParserContext *xmlXPathParserContextPtr;
typedef enum { XPATH_EXPRESSION_OK = 0, XPATH_NUMBER_ERROR, XPATH_UNFINISHED_LITERAL_ERROR, XPATH_START_LITERAL_ERROR, XPATH_VARIABLE_REF_ERROR, XPATH_UNDEF_VARIABLE_ERROR, XPATH_INVALID_PREDICATE_ERROR, XPATH_EXPR_ERROR, XPATH_UNCLOSED_ERROR, XPATH_UNKNOWN_FUNC_ERROR, XPATH_INVALID_OPERAND, XPATH_INVALID_TYPE, XPATH_INVALID_ARITY, XPATH_INVALID_CTXT_SIZE, XPATH_INVALID_CTXT_POSITION, XPATH_MEMORY_ERROR, XPTR_SYNTAX_ERROR, XPTR_RESOURCE_ERROR, XPTR_SUB_RESOURCE_ERROR, XPATH_UNDEF_PREFIX_ERROR, XPATH_ENCODING_ERROR, XPATH_INVALID_CHAR_ERROR, XPATH_INVALID_CTXT, XPATH_STACK_ERROR, XPATH_FORBID_VARIABLE_ERROR, XPATH_OP_LIMIT_EXCEEDED, XPATH_RECURSION_LIMIT_EXCEEDED } xmlXPathError;
typedef struct _xmlNodeSet xmlNodeSet;
typedef xmlNodeSet *xmlNodeSetPtr;
struct _xmlNodeSet { int nodeNr; int nodeMax; xmlNode **nodeTab; };
typedef enum { XPATH_UNDEFINED = 0, XPATH_NODESET = 1, XPATH_BOOLEAN = 2, XPATH_NUMBER = 3, XPATH_STRING = 4, XPATH_USERS = 8, XPATH_XSLT_TREE = 9 } xmlXPathObjectType;
typedef struct _xmlXPathObject xmlXPathObject;
typedef xmlXPathObject *xmlXPathObjectPtr;
struct _xmlXPathObject { xmlXPathObjectType type; xmlNodeSet *nodesetval; int boolval; double floatval; xmlChar *stringval; void *user; int index; void *user2; int index2; };
typedef int (*xmlXPathConvertFunc) (xmlXPathObject *obj, int type);
typedef struct _xmlXPathType xmlXPathType;
typedef xmlXPathType *xmlXPathTypePtr;
struct _xmlXPathType { const xmlChar *name; xmlXPathConvertFunc func; };
typedef struct _xmlXPathVariable xmlXPathVariable;
typedef xmlXPathVariable *xmlXPathVariablePtr;
struct _xmlXPathVariable { const xmlChar *name; xmlXPathObject *value; };
typedef void (*xmlXPathEvalFunc)(xmlXPathParserContext *ctxt, int nargs);
typedef struct _xmlXPathFunct xmlXPathFunct;
typedef xmlXPathFunct *xmlXPathFuncPtr;
struct _xmlXPathFunct { const xmlChar *name; xmlXPathEvalFunc func; };
typedef xmlXPathObject *(*xmlXPathAxisFunc) (xmlXPathParserContext *ctxt, xmlXPathObject *cur);
typedef struct _xmlXPathAxis xmlXPathAxis;
typedef xmlXPathAxis *xmlXPathAxisPtr;
struct _xmlXPathAxis { const xmlChar *name; xmlXPathAxisFunc func; };
typedef void (*xmlXPathFunction) (xmlXPathParserContext *ctxt, int nargs);
typedef xmlXPathObject *(*xmlXPathVariableLookupFunc) (void *ctxt, const xmlChar *name, const xmlChar *ns_uri);
typedef xmlXPathFunction (*xmlXPathFuncLookupFunc) (void *ctxt, const xmlChar *name, const xmlChar *ns_uri);
struct _xmlXPathContext { xmlDoc *doc; xmlNode *node; int nb_variables_unused; int max_variables_unused; xmlHashTable *varHash; int nb_types; int max_types; xmlXPathType *types; int nb_funcs_unused; int max_funcs_unused; xmlHashTable *funcHash; int nb_axis; int max_axis; xmlXPathAxis *axis; xmlNs **namespaces; int nsNr; void *user; int contextSize; int proximityPosition; int xptr; xmlNode *here; xmlNode *origin; xmlHashTable *nsHash; xmlXPathVariableLookupFunc varLookupFunc; void *varLookupData; void *extra; const xmlChar *function; const xmlChar *functionURI; xmlXPathFuncLookupFunc funcLookupFunc; void *funcLookupData; xmlNs **tmpNsList; int tmpNsNr; void *userData; xmlStructuredErrorFunc error; xmlError lastError; xmlNode *debugNode; xmlDict *dict; int flags; void *cache; unsigned long opLimit; unsigned long opCount; int depth; };
typedef struct _xmlXPathCompExpr xmlXPathCompExpr;
typedef xmlXPathCompExpr *xmlXPathCompExprPtr;
struct _xmlXPathParserContext { const xmlChar *cur; const xmlChar *base; int error; xmlXPathContext *context; xmlXPathObject *value; int valueNr; int valueMax; xmlXPathObject **valueTab; xmlXPathCompExpr *comp; int xptr; xmlNode *ancestor; int valueFrame; };
typedef enum { XML_C14N_1_0 = 0, XML_C14N_EXCLUSIVE_1_0 = 1, XML_C14N_1_1 = 2 } xmlC14NMode;
typedef int (*xmlC14NIsVisibleCallback) (void* user_data, xmlNode *node, xmlNode *parent);
typedef enum { XML_CATA_PREFER_NONE = 0, XML_CATA_PREFER_PUBLIC = 1, XML_CATA_PREFER_SYSTEM } xmlCatalogPrefer;
typedef enum { XML_CATA_ALLOW_NONE = 0, XML_CATA_ALLOW_GLOBAL = 1, XML_CATA_ALLOW_DOCUMENT = 2, XML_CATA_ALLOW_ALL = 3 } xmlCatalogAllow;
typedef struct _xmlCatalog xmlCatalog;
typedef xmlCatalog *xmlCatalogPtr;
typedef struct _xmlChSRange xmlChSRange;
typedef xmlChSRange *xmlChSRangePtr;
struct _xmlChSRange { unsigned short low; unsigned short high; };
typedef struct _xmlChLRange xmlChLRange;
typedef xmlChLRange *xmlChLRangePtr;
struct _xmlChLRange { unsigned int low; unsigned int high; };
typedef struct _xmlChRangeGroup xmlChRangeGroup;
typedef xmlChRangeGroup *xmlChRangeGroupPtr;
struct _xmlChRangeGroup { int nbShortRange; int nbLongRange; const xmlChSRange *shortRange; const xmlChLRange *longRange; };
typedef xmlParserCtxt htmlParserCtxt;
typedef xmlParserCtxtPtr htmlParserCtxtPtr;
typedef xmlParserNodeInfo htmlParserNodeInfo;
typedef xmlSAXHandler htmlSAXHandler;
typedef xmlSAXHandlerPtr htmlSAXHandlerPtr;
typedef xmlParserInput htmlParserInput;
typedef xmlParserInputPtr htmlParserInputPtr;
typedef xmlDocPtr htmlDocPtr;
typedef xmlNodePtr htmlNodePtr;
typedef struct _htmlElemDesc htmlElemDesc;
typedef htmlElemDesc *htmlElemDescPtr;
struct _htmlElemDesc { const char *name; char startTag; char endTag; char saveEndTag; char empty; char depr; char dtd; char isinline; const char *desc; const char** subelts __attribute__((deprecated)); const char* defaultsubelt __attribute__((deprecated)); const char** attrs_opt __attribute__((deprecated)); const char** attrs_depr __attribute__((deprecated)); const char** attrs_req __attribute__((deprecated)); int dataMode; };
typedef struct _htmlEntityDesc htmlEntityDesc;
typedef htmlEntityDesc *htmlEntityDescPtr;
struct _htmlEntityDesc { unsigned int value; const char *name; const char *desc; };
typedef enum { HTML_PARSE_RECOVER = 1<<0, HTML_PARSE_NODEFDTD = 1<<2, HTML_PARSE_NOERROR = 1<<5, HTML_PARSE_NOWARNING = 1<<6, HTML_PARSE_PEDANTIC = 1<<7, HTML_PARSE_NOBLANKS = 1<<8, HTML_PARSE_NONET = 1<<11, HTML_PARSE_NOIMPLIED = 1<<13, HTML_PARSE_COMPACT = 1<<16, HTML_PARSE_HUGE = 1<<19, HTML_PARSE_IGNORE_ENC =1<<21, HTML_PARSE_BIG_LINES = 1<<22, HTML_PARSE_HTML5 = 1<<26 } htmlParserOption;
typedef enum { HTML_NA = 0 , HTML_INVALID = 0x1 , HTML_DEPRECATED = 0x2 , HTML_VALID = 0x4 , HTML_REQUIRED = 0xc } htmlStatus;
typedef enum { XML_SAVE_FORMAT = 1<<0, XML_SAVE_NO_DECL = 1<<1, XML_SAVE_NO_EMPTY = 1<<2, XML_SAVE_NO_XHTML = 1<<3, XML_SAVE_XHTML = 1<<4, XML_SAVE_AS_XML = 1<<5, XML_SAVE_AS_HTML = 1<<6, XML_SAVE_WSNONSIG = 1<<7, XML_SAVE_EMPTY = 1<<8, XML_SAVE_NO_INDENT = 1<<9, XML_SAVE_INDENT = 1<<10 } xmlSaveOption;
typedef struct _xmlSaveCtxt xmlSaveCtxt;
typedef xmlSaveCtxt *xmlSaveCtxtPtr;
typedef struct _xmlPattern xmlPattern;
typedef xmlPattern *xmlPatternPtr;
typedef enum { XML_PATTERN_DEFAULT = 0, XML_PATTERN_XPATH = 1<<0, XML_PATTERN_XSSEL = 1<<1, XML_PATTERN_XSFIELD = 1<<2 } xmlPatternFlags;
typedef struct _xmlStreamCtxt xmlStreamCtxt;
typedef xmlStreamCtxt *xmlStreamCtxtPtr;
typedef struct _xmlRelaxNG xmlRelaxNG;
typedef xmlRelaxNG *xmlRelaxNGPtr;
typedef void (*xmlRelaxNGValidityErrorFunc) (void *ctx, const char *msg, ...) __attribute__((__format__(__printf__,2,3)));
typedef void (*xmlRelaxNGValidityWarningFunc) (void *ctx, const char *msg, ...) __attribute__((__format__(__printf__,2,3)));
typedef struct _xmlRelaxNGParserCtxt xmlRelaxNGParserCtxt;
typedef xmlRelaxNGParserCtxt *xmlRelaxNGParserCtxtPtr;
typedef struct _xmlRelaxNGValidCtxt xmlRelaxNGValidCtxt;
typedef xmlRelaxNGValidCtxt *xmlRelaxNGValidCtxtPtr;
typedef enum { XML_RELAXNG_OK = 0, XML_RELAXNG_ERR_MEMORY, XML_RELAXNG_ERR_TYPE, XML_RELAXNG_ERR_TYPEVAL, XML_RELAXNG_ERR_DUPID, XML_RELAXNG_ERR_TYPECMP, XML_RELAXNG_ERR_NOSTATE, XML_RELAXNG_ERR_NODEFINE, XML_RELAXNG_ERR_LISTEXTRA, XML_RELAXNG_ERR_LISTEMPTY, XML_RELAXNG_ERR_INTERNODATA, XML_RELAXNG_ERR_INTERSEQ, XML_RELAXNG_ERR_INTEREXTRA, XML_RELAXNG_ERR_ELEMNAME, XML_RELAXNG_ERR_ATTRNAME, XML_RELAXNG_ERR_ELEMNONS, XML_RELAXNG_ERR_ATTRNONS, XML_RELAXNG_ERR_ELEMWRONGNS, XML_RELAXNG_ERR_ATTRWRONGNS, XML_RELAXNG_ERR_ELEMEXTRANS, XML_RELAXNG_ERR_ATTREXTRANS, XML_RELAXNG_ERR_ELEMNOTEMPTY, XML_RELAXNG_ERR_NOELEM, XML_RELAXNG_ERR_NOTELEM, XML_RELAXNG_ERR_ATTRVALID, XML_RELAXNG_ERR_CONTENTVALID, XML_RELAXNG_ERR_EXTRACONTENT, XML_RELAXNG_ERR_INVALIDATTR, XML_RELAXNG_ERR_DATAELEM, XML_RELAXNG_ERR_VALELEM, XML_RELAXNG_ERR_LISTELEM, XML_RELAXNG_ERR_DATATYPE, XML_RELAXNG_ERR_VALUE, XML_RELAXNG_ERR_LIST, XML_RELAXNG_ERR_NOGRAMMAR, XML_RELAXNG_ERR_EXTRADATA, XML_RELAXNG_ERR_LACKDATA, XML_RELAXNG_ERR_INTERNAL, XML_RELAXNG_ERR_ELEMWRONG, XML_RELAXNG_ERR_TEXTWRONG } xmlRelaxNGValidErr;
typedef enum { XML_RELAXNGP_NONE = 0, XML_RELAXNGP_FREE_DOC = 1, XML_RELAXNGP_CRNG = 2 } xmlRelaxNGParserFlag;
typedef enum { XML_SCHEMAS_UNKNOWN = 0, XML_SCHEMAS_STRING = 1, XML_SCHEMAS_NORMSTRING = 2, XML_SCHEMAS_DECIMAL = 3, XML_SCHEMAS_TIME = 4, XML_SCHEMAS_GDAY = 5, XML_SCHEMAS_GMONTH = 6, XML_SCHEMAS_GMONTHDAY = 7, XML_SCHEMAS_GYEAR = 8, XML_SCHEMAS_GYEARMONTH = 9, XML_SCHEMAS_DATE = 10, XML_SCHEMAS_DATETIME = 11, XML_SCHEMAS_DURATION = 12, XML_SCHEMAS_FLOAT = 13, XML_SCHEMAS_DOUBLE = 14, XML_SCHEMAS_BOOLEAN = 15, XML_SCHEMAS_TOKEN = 16, XML_SCHEMAS_LANGUAGE = 17, XML_SCHEMAS_NMTOKEN = 18, XML_SCHEMAS_NMTOKENS = 19, XML_SCHEMAS_NAME = 20, XML_SCHEMAS_QNAME = 21, XML_SCHEMAS_NCNAME = 22, XML_SCHEMAS_ID = 23, XML_SCHEMAS_IDREF = 24, XML_SCHEMAS_IDREFS = 25, XML_SCHEMAS_ENTITY = 26, XML_SCHEMAS_ENTITIES = 27, XML_SCHEMAS_NOTATION = 28, XML_SCHEMAS_ANYURI = 29, XML_SCHEMAS_INTEGER = 30, XML_SCHEMAS_NPINTEGER = 31, XML_SCHEMAS_NINTEGER = 32, XML_SCHEMAS_NNINTEGER = 33, XML_SCHEMAS_PINTEGER = 34, XML_SCHEMAS_INT = 35, XML_SCHEMAS_UINT = 36, XML_SCHEMAS_LONG = 37, XML_SCHEMAS_ULONG = 38, XML_SCHEMAS_SHORT = 39, XML_SCHEMAS_USHORT = 40, XML_SCHEMAS_BYTE = 41, XML_SCHEMAS_UBYTE = 42, XML_SCHEMAS_HEXBINARY = 43, XML_SCHEMAS_BASE64BINARY = 44, XML_SCHEMAS_ANYTYPE = 45, XML_SCHEMAS_ANYSIMPLETYPE = 46 } xmlSchemaValType;
typedef enum { XML_SCHEMA_TYPE_BASIC = 1, XML_SCHEMA_TYPE_ANY, XML_SCHEMA_TYPE_FACET, XML_SCHEMA_TYPE_SIMPLE, XML_SCHEMA_TYPE_COMPLEX, XML_SCHEMA_TYPE_SEQUENCE = 6, XML_SCHEMA_TYPE_CHOICE, XML_SCHEMA_TYPE_ALL, XML_SCHEMA_TYPE_SIMPLE_CONTENT, XML_SCHEMA_TYPE_COMPLEX_CONTENT, XML_SCHEMA_TYPE_UR, XML_SCHEMA_TYPE_RESTRICTION, XML_SCHEMA_TYPE_EXTENSION, XML_SCHEMA_TYPE_ELEMENT, XML_SCHEMA_TYPE_ATTRIBUTE, XML_SCHEMA_TYPE_ATTRIBUTEGROUP, XML_SCHEMA_TYPE_GROUP, XML_SCHEMA_TYPE_NOTATION, XML_SCHEMA_TYPE_LIST, XML_SCHEMA_TYPE_UNION, XML_SCHEMA_TYPE_ANY_ATTRIBUTE, XML_SCHEMA_TYPE_IDC_UNIQUE, XML_SCHEMA_TYPE_IDC_KEY, XML_SCHEMA_TYPE_IDC_KEYREF, XML_SCHEMA_TYPE_PARTICLE = 25, XML_SCHEMA_TYPE_ATTRIBUTE_USE, XML_SCHEMA_FACET_MININCLUSIVE = 1000, XML_SCHEMA_FACET_MINEXCLUSIVE, XML_SCHEMA_FACET_MAXINCLUSIVE, XML_SCHEMA_FACET_MAXEXCLUSIVE, XML_SCHEMA_FACET_TOTALDIGITS, XML_SCHEMA_FACET_FRACTIONDIGITS, XML_SCHEMA_FACET_PATTERN, XML_SCHEMA_FACET_ENUMERATION, XML_SCHEMA_FACET_WHITESPACE, XML_SCHEMA_FACET_LENGTH, XML_SCHEMA_FACET_MAXLENGTH, XML_SCHEMA_FACET_MINLENGTH, XML_SCHEMA_EXTRA_QNAMEREF = 2000, XML_SCHEMA_EXTRA_ATTR_USE_PROHIB } xmlSchemaTypeType;
typedef enum { XML_SCHEMA_CONTENT_UNKNOWN = 0, XML_SCHEMA_CONTENT_EMPTY = 1, XML_SCHEMA_CONTENT_ELEMENTS, XML_SCHEMA_CONTENT_MIXED, XML_SCHEMA_CONTENT_SIMPLE, XML_SCHEMA_CONTENT_MIXED_OR_ELEMENTS, XML_SCHEMA_CONTENT_BASIC, XML_SCHEMA_CONTENT_ANY } xmlSchemaContentType;
typedef struct _xmlSchemaVal xmlSchemaVal;
typedef xmlSchemaVal *xmlSchemaValPtr;
typedef struct _xmlSchemaType xmlSchemaType;
typedef xmlSchemaType *xmlSchemaTypePtr;
typedef struct _xmlSchemaFacet xmlSchemaFacet;
typedef xmlSchemaFacet *xmlSchemaFacetPtr;
typedef struct _xmlSchemaAnnot xmlSchemaAnnot;
typedef xmlSchemaAnnot *xmlSchemaAnnotPtr;
struct _xmlSchemaAnnot { struct _xmlSchemaAnnot *next; xmlNode *content; };
typedef struct _xmlSchemaAttribute xmlSchemaAttribute;
typedef xmlSchemaAttribute *xmlSchemaAttributePtr;
struct _xmlSchemaAttribute { xmlSchemaTypeType type; struct _xmlSchemaAttribute *next; const xmlChar *name; const xmlChar *id; const xmlChar *ref; const xmlChar *refNs; const xmlChar *typeName; const xmlChar *typeNs; xmlSchemaAnnot *annot; xmlSchemaType *base; int occurs; const xmlChar *defValue; xmlSchemaType *subtypes; xmlNode *node; const xmlChar *targetNamespace; int flags; const xmlChar *refPrefix; xmlSchemaVal *defVal; xmlSchemaAttribute *refDecl; };
typedef struct _xmlSchemaAttributeLink xmlSchemaAttributeLink;
typedef xmlSchemaAttributeLink *xmlSchemaAttributeLinkPtr;
struct _xmlSchemaAttributeLink { struct _xmlSchemaAttributeLink *next; struct _xmlSchemaAttribute *attr; };
typedef struct _xmlSchemaWildcardNs xmlSchemaWildcardNs;
typedef xmlSchemaWildcardNs *xmlSchemaWildcardNsPtr;
struct _xmlSchemaWildcardNs { struct _xmlSchemaWildcardNs *next; const xmlChar *value; };
typedef struct _xmlSchemaWildcard xmlSchemaWildcard;
typedef xmlSchemaWildcard *xmlSchemaWildcardPtr;
struct _xmlSchemaWildcard { xmlSchemaTypeType type; const xmlChar *id; xmlSchemaAnnot *annot; xmlNode *node; int minOccurs; int maxOccurs; int processContents; int any; xmlSchemaWildcardNs *nsSet; xmlSchemaWildcardNs *negNsSet; int flags; };
typedef struct _xmlSchemaAttributeGroup xmlSchemaAttributeGroup;
typedef xmlSchemaAttributeGroup *xmlSchemaAttributeGroupPtr;
struct _xmlSchemaAttributeGroup { xmlSchemaTypeType type; struct _xmlSchemaAttribute *next; const xmlChar *name; const xmlChar *id; const xmlChar *ref; const xmlChar *refNs; xmlSchemaAnnot *annot; xmlSchemaAttribute *attributes; xmlNode *node; int flags; xmlSchemaWildcard *attributeWildcard; const xmlChar *refPrefix; xmlSchemaAttributeGroup *refItem; const xmlChar *targetNamespace; void *attrUses; };
typedef struct _xmlSchemaTypeLink xmlSchemaTypeLink;
typedef xmlSchemaTypeLink *xmlSchemaTypeLinkPtr;
struct _xmlSchemaTypeLink { struct _xmlSchemaTypeLink *next; xmlSchemaType *type; };
typedef struct _xmlSchemaFacetLink xmlSchemaFacetLink;
typedef xmlSchemaFacetLink *xmlSchemaFacetLinkPtr;
struct _xmlSchemaFacetLink { struct _xmlSchemaFacetLink *next; xmlSchemaFacet *facet; };
struct _xmlSchemaType { xmlSchemaTypeType type; struct _xmlSchemaType *next; const xmlChar *name; const xmlChar *id ; const xmlChar *ref; const xmlChar *refNs; xmlSchemaAnnot *annot; xmlSchemaType *subtypes; xmlSchemaAttribute *attributes; xmlNode *node; int minOccurs; int maxOccurs; int flags; xmlSchemaContentType contentType; const xmlChar *base; const xmlChar *baseNs; xmlSchemaType *baseType; xmlSchemaFacet *facets; struct _xmlSchemaType *redef; int recurse; xmlSchemaAttributeLink **attributeUses; xmlSchemaWildcard *attributeWildcard; int builtInType; xmlSchemaTypeLink *memberTypes; xmlSchemaFacetLink *facetSet; const xmlChar *refPrefix; xmlSchemaType *contentTypeDef; xmlRegexp *contModel; const xmlChar *targetNamespace; void *attrUses; };
typedef struct _xmlSchemaElement xmlSchemaElement;
typedef xmlSchemaElement *xmlSchemaElementPtr;
struct _xmlSchemaElement { xmlSchemaTypeType type; struct _xmlSchemaType *next; const xmlChar *name; const xmlChar *id; const xmlChar *ref; const xmlChar *refNs; xmlSchemaAnnot *annot; xmlSchemaType *subtypes; xmlSchemaAttribute *attributes; xmlNode *node; int minOccurs; int maxOccurs; int flags; const xmlChar *targetNamespace; const xmlChar *namedType; const xmlChar *namedTypeNs; const xmlChar *substGroup; const xmlChar *substGroupNs; const xmlChar *scope; const xmlChar *value; struct _xmlSchemaElement *refDecl; xmlRegexp *contModel; xmlSchemaContentType contentType; const xmlChar *refPrefix; xmlSchemaVal *defVal; void *idcs; };
struct _xmlSchemaFacet { xmlSchemaTypeType type; struct _xmlSchemaFacet *next; const xmlChar *value; const xmlChar *id; xmlSchemaAnnot *annot; xmlNode *node; int fixed; int whitespace; xmlSchemaVal *val; xmlRegexp *regexp; };
typedef struct _xmlSchemaNotation xmlSchemaNotation;
typedef xmlSchemaNotation *xmlSchemaNotationPtr;
struct _xmlSchemaNotation { xmlSchemaTypeType type; const xmlChar *name; xmlSchemaAnnot *annot; const xmlChar *identifier; const xmlChar *targetNamespace; };
struct _xmlSchema { const xmlChar *name; const xmlChar *targetNamespace; const xmlChar *version; const xmlChar *id; xmlDoc *doc; xmlSchemaAnnot *annot; int flags; xmlHashTable *typeDecl; xmlHashTable *attrDecl; xmlHashTable *attrgrpDecl; xmlHashTable *elemDecl; xmlHashTable *notaDecl; xmlHashTable *schemasImports; void *_private; xmlHashTable *groupDecl; xmlDict *dict; void *includes; int preserve; int counter; xmlHashTable *idcDef; void *volatiles; };
typedef enum { XML_SCHEMATRON_OUT_QUIET = 1 << 0, XML_SCHEMATRON_OUT_TEXT = 1 << 1, XML_SCHEMATRON_OUT_XML = 1 << 2, XML_SCHEMATRON_OUT_ERROR = 1 << 3, XML_SCHEMATRON_OUT_FILE = 1 << 8, XML_SCHEMATRON_OUT_BUFFER = 1 << 9, XML_SCHEMATRON_OUT_IO = 1 << 10 } xmlSchematronValidOptions;
typedef struct _xmlSchematron xmlSchematron;
typedef xmlSchematron *xmlSchematronPtr;
typedef void (*xmlSchematronValidityErrorFunc) (void *ctx, const char *msg, ...);
typedef void (*xmlSchematronValidityWarningFunc) (void *ctx, const char *msg, ...);
typedef struct _xmlSchematronParserCtxt xmlSchematronParserCtxt;
typedef xmlSchematronParserCtxt *xmlSchematronParserCtxtPtr;
typedef struct _xmlSchematronValidCtxt xmlSchematronValidCtxt;
typedef xmlSchematronValidCtxt *xmlSchematronValidCtxtPtr;
typedef struct _xmlURI xmlURI;
typedef xmlURI *xmlURIPtr;
struct _xmlURI { char *scheme; char *opaque; char *authority; char *server; char *user; int port; char *path; char *query; char *fragment; int cleanup; char *query_raw; };
typedef struct _xmlXIncludeCtxt xmlXIncludeCtxt;
typedef xmlXIncludeCtxt *xmlXIncludeCtxtPtr;
typedef xmlChar *xlinkHRef;
typedef xmlChar *xlinkRole;
typedef xmlChar *xlinkTitle;
typedef enum { XLINK_TYPE_NONE = 0, XLINK_TYPE_SIMPLE, XLINK_TYPE_EXTENDED, XLINK_TYPE_EXTENDED_SET } xlinkType;
typedef enum { XLINK_SHOW_NONE = 0, XLINK_SHOW_NEW, XLINK_SHOW_EMBED, XLINK_SHOW_REPLACE } xlinkShow;
typedef enum { XLINK_ACTUATE_NONE = 0, XLINK_ACTUATE_AUTO, XLINK_ACTUATE_ONREQUEST } xlinkActuate;
typedef void (*xlinkNodeDetectFunc) (void *ctx, xmlNode *node);
typedef void (*xlinkSimpleLinkFunk) (void *ctx, xmlNode *node, const xlinkHRef href, const xlinkRole role, const xlinkTitle title);
typedef void (*xlinkExtendedLinkFunk)(void *ctx, xmlNode *node, int nbLocators, const xlinkHRef *hrefs, const xlinkRole *roles, int nbArcs, const xlinkRole *from, const xlinkRole *to, xlinkShow *show, xlinkActuate *actuate, int nbTitles, const xlinkTitle *titles, const xmlChar **langs);
typedef void (*xlinkExtendedLinkSetFunk) (void *ctx, xmlNode *node, int nbLocators, const xlinkHRef *hrefs, const xlinkRole *roles, int nbTitles, const xlinkTitle *titles, const xmlChar **langs);
typedef struct _xlinkHandler xlinkHandler;
typedef xlinkHandler *xlinkHandlerPtr;
struct _xlinkHandler { xlinkSimpleLinkFunk simple; xlinkExtendedLinkFunk extended; xlinkExtendedLinkSetFunk set; };
typedef struct _xmlModule xmlModule;
typedef xmlModule *xmlModulePtr;
typedef enum { XML_MODULE_LAZY = 1, XML_MODULE_LOCAL= 2 } xmlModuleOption;
typedef enum { XML_SCHEMAS_ERR_OK = 0, XML_SCHEMAS_ERR_NOROOT = 1, XML_SCHEMAS_ERR_UNDECLAREDELEM, XML_SCHEMAS_ERR_NOTTOPLEVEL, XML_SCHEMAS_ERR_MISSING, XML_SCHEMAS_ERR_WRONGELEM, XML_SCHEMAS_ERR_NOTYPE, XML_SCHEMAS_ERR_NOROLLBACK, XML_SCHEMAS_ERR_ISABSTRACT, XML_SCHEMAS_ERR_NOTEMPTY, XML_SCHEMAS_ERR_ELEMCONT, XML_SCHEMAS_ERR_HAVEDEFAULT, XML_SCHEMAS_ERR_NOTNILLABLE, XML_SCHEMAS_ERR_EXTRACONTENT, XML_SCHEMAS_ERR_INVALIDATTR, XML_SCHEMAS_ERR_INVALIDELEM, XML_SCHEMAS_ERR_NOTDETERMINIST, XML_SCHEMAS_ERR_CONSTRUCT, XML_SCHEMAS_ERR_INTERNAL, XML_SCHEMAS_ERR_NOTSIMPLE, XML_SCHEMAS_ERR_ATTRUNKNOWN, XML_SCHEMAS_ERR_ATTRINVALID, XML_SCHEMAS_ERR_VALUE, XML_SCHEMAS_ERR_FACET, XML_SCHEMAS_ERR_, XML_SCHEMAS_ERR_XXX } xmlSchemaValidError;
typedef enum { XML_SCHEMA_VAL_VC_I_CREATE = 1<<0 } xmlSchemaValidOption;
typedef struct _xmlSchema xmlSchema;
typedef xmlSchema *xmlSchemaPtr;
typedef void (*xmlSchemaValidityErrorFunc) (void *ctx, const char *msg, ...) __attribute__((__format__(__printf__,2,3)));
typedef void (*xmlSchemaValidityWarningFunc) (void *ctx, const char *msg, ...) __attribute__((__format__(__printf__,2,3)));
typedef struct _xmlSchemaParserCtxt xmlSchemaParserCtxt;
typedef xmlSchemaParserCtxt *xmlSchemaParserCtxtPtr;
typedef struct _xmlSchemaValidCtxt xmlSchemaValidCtxt;
typedef xmlSchemaValidCtxt *xmlSchemaValidCtxtPtr;
typedef int (*xmlSchemaValidityLocatorFunc) (void *ctx, const char **file, unsigned long *line);
typedef struct _xmlSchemaSAXPlug xmlSchemaSAXPlugStruct;
typedef xmlSchemaSAXPlugStruct *xmlSchemaSAXPlugPtr;
typedef enum { XML_PARSER_SEVERITY_VALIDITY_WARNING = 1, XML_PARSER_SEVERITY_VALIDITY_ERROR = 2, XML_PARSER_SEVERITY_WARNING = 3, XML_PARSER_SEVERITY_ERROR = 4 } xmlParserSeverities;
typedef enum { XML_TEXTREADER_MODE_INITIAL = 0, XML_TEXTREADER_MODE_INTERACTIVE = 1, XML_TEXTREADER_MODE_ERROR = 2, XML_TEXTREADER_MODE_EOF =3, XML_TEXTREADER_MODE_CLOSED = 4, XML_TEXTREADER_MODE_READING = 5 } xmlTextReaderMode;
typedef enum { XML_PARSER_LOADDTD = 1, XML_PARSER_DEFAULTATTRS = 2, XML_PARSER_VALIDATE = 3, XML_PARSER_SUBST_ENTITIES = 4 } xmlParserProperties;
typedef enum { XML_READER_TYPE_NONE = 0, XML_READER_TYPE_ELEMENT = 1, XML_READER_TYPE_ATTRIBUTE = 2, XML_READER_TYPE_TEXT = 3, XML_READER_TYPE_CDATA = 4, XML_READER_TYPE_ENTITY_REFERENCE = 5, XML_READER_TYPE_ENTITY = 6, XML_READER_TYPE_PROCESSING_INSTRUCTION = 7, XML_READER_TYPE_COMMENT = 8, XML_READER_TYPE_DOCUMENT = 9, XML_READER_TYPE_DOCUMENT_TYPE = 10, XML_READER_TYPE_DOCUMENT_FRAGMENT = 11, XML_READER_TYPE_NOTATION = 12, XML_READER_TYPE_WHITESPACE = 13, XML_READER_TYPE_SIGNIFICANT_WHITESPACE = 14, XML_READER_TYPE_END_ELEMENT = 15, XML_READER_TYPE_END_ENTITY = 16, XML_READER_TYPE_XML_DECLARATION = 17 } xmlReaderTypes;
typedef struct _xmlTextReader xmlTextReader;
typedef xmlTextReader *xmlTextReaderPtr;
typedef void * xmlTextReaderLocatorPtr;
typedef void (*xmlTextReaderErrorFunc)(void *arg, const char *msg, xmlParserSeverities severity, xmlTextReaderLocatorPtr locator);
typedef enum { XML_SCHEMA_WHITESPACE_UNKNOWN = 0, XML_SCHEMA_WHITESPACE_PRESERVE = 1, XML_SCHEMA_WHITESPACE_REPLACE = 2, XML_SCHEMA_WHITESPACE_COLLAPSE = 3 } xmlSchemaWhitespaceValueType;
typedef struct _xmlTextWriter xmlTextWriter;
typedef xmlTextWriter *xmlTextWriterPtr;

/* ============================================================
 * 数据符号（nm -D 类型 D/R/B）：定义为同名真实变量，
 * 加载成功后由 libxml2_sync_globals() 从目标库拷贝值。
 * 头文件声明为 const 的对象此处以非 const 定义，仅本文件在
 * 加载时写入一次；对调用方语义不变（调用方仍只读）。
 * ============================================================ */
xmlFreeFunc      xmlFree          = NULL;
xmlMallocFunc    xmlMalloc        = NULL;
xmlMallocFunc    xmlMallocAtomic  = NULL;
xmlReallocFunc   xmlRealloc       = NULL;
xmlStrdupFunc    xmlMemStrdup     = NULL;

const char      *xmlParserVersion = NULL;

xmlChar          xmlStringText[16];
xmlChar          xmlStringTextNoenc[16];
xmlChar          xmlStringComment[16];
unsigned char    xmlIsPubidChar_tab[256];

xmlSAXHandlerV1  xmlDefaultSAXHandler;
xmlSAXHandlerV1  htmlDefaultSAXHandler;
xmlSAXLocator    xmlDefaultSAXLocator;

xmlChRangeGroup  xmlIsBaseCharGroup;
xmlChRangeGroup  xmlIsCharGroup;
xmlChRangeGroup  xmlIsCombiningGroup;
xmlChRangeGroup  xmlIsDigitGroup;
xmlChRangeGroup  xmlIsExtenderGroup;
xmlChRangeGroup  xmlIsIdeographicGroup;

double           xmlXPathNAN;
double           xmlXPathPINF;
double           xmlXPathNINF;

void libxml2_dyn_sync_globals(void);


/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t libxml2_handle = NULL;
static void *libxml2_sym(const char *name) {
    return libxml2_handle ? get_sym(libxml2_handle, name) : NULL;
}
static int libxml2_init_state = 0;

static int libxml2_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;
    if (libxml2_handle) return 0;
    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("LIBXML2_DYN_LIB");
        if (e && *e) {
            snprintf(bufs[n], sizeof bufs[0], "%s", e);
            candidates[n] = bufs[n]; n++;
        }
    }
    {
#ifdef _WIN32
        const char *env_path = getenv("PATH");
#elif defined(__APPLE__)
        const char *env_path = getenv("DYLD_LIBRARY_PATH");
#else
        const char *env_path = getenv("LD_LIBRARY_PATH");
#endif
        if (env_path && *env_path) {
            char *copy = strdup(env_path);
            if (copy) {
                char *q = copy;
                while (q && *q && n < 80) {
                    char *sep = strchr(q, PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/libxml2.so.2", q);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/libxml2.so.16", q);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/libxml2.so", q);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = "libxml2.so.2";
    candidates[n++] = "libxml2.so.16";
    candidates[n++] = "libxml2.so";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libxml2.so.2", home);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libxml2.so.16", home);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libxml2.so", home);
            candidates[n] = bufs[n]; n++;
        }
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libxml2.so.2";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libxml2.so.16";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libxml2.so";
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libxml2.so.2";
    candidates[n++] = "/usr/local/lib/libxml2.so.2";
    candidates[n++] = "/opt/homebrew/lib/libxml2.so.16";
    candidates[n++] = "/usr/local/lib/libxml2.so.16";
    candidates[n++] = "/opt/homebrew/lib/libxml2.so";
    candidates[n++] = "/usr/local/lib/libxml2.so";
#elif defined(_WIN32)
    candidates[n++] = "libxml2.dll";
#else
    candidates[n++] = "/usr/local/lib/libxml2.so.2";
    candidates[n++] = "/usr/lib/libxml2.so.2";
    candidates[n++] = "/usr/local/lib/libxml2.so.16";
    candidates[n++] = "/usr/lib/libxml2.so.16";
    candidates[n++] = "/usr/local/lib/libxml2.so";
    candidates[n++] = "/usr/lib/libxml2.so";
#endif
    for (i = 0; i < n; i++) {
        libxml2_handle = load_lib(candidates[i]);
        if (libxml2_handle) break;
    }
    if (!libxml2_handle || !libxml2_sym("xmlReadMemory")) {
        fprintf(stderr, "libxml2_dyn: cannot load libxml2 (%s)\n", lib_error());
        if (libxml2_handle) { free_lib(libxml2_handle); libxml2_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "libxml2_dyn: loaded libxml2 (2.15 headers)\n");
    libxml2_dyn_sync_globals();
    return 0;
}

int libxml2_dyn_libok(void) {
    if (libxml2_init_state == 0)
        libxml2_init_state = (libxml2_do_load(NULL) == 0) ? 1 : -1;
    return (libxml2_init_state == 1 && libxml2_handle != NULL) ? 1 : 0;
}

int libxml2_dyn_init(const char *library_path) {
    if (libxml2_do_load(library_path) == 0) {
        libxml2_init_state = 1;
        return 0;
    }
    if (libxml2_init_state == 0) libxml2_init_state = -1;
    return -1;
}

void libxml2_dyn_cleanup(void) {
    /* 仅复位加载状态，不卸载已成功加载的库（dlclose 不安全） */
    if (!libxml2_handle) libxml2_init_state = 0;
    else if (libxml2_init_state == -1) libxml2_init_state = 0;
}

int libxml2_dyn_has_symbol(const char *name) {
    if (!libxml2_dyn_libok()) return 0;
    return libxml2_sym(name) != NULL;
}

/* ============================================================
 * 数据符号值同步（目标库 dlopen 后调用一次）
 * xmlMemSetup 等改变 xmlFree/xmlMalloc 后可再调用本函数重新同步
 * ============================================================ */
void libxml2_dyn_sync_globals(void) {
    void *p;
    if ((p = libxml2_sym("xmlFree")) != NULL)          xmlFree = *(xmlFreeFunc *)p;
    if ((p = libxml2_sym("xmlMalloc")) != NULL)        xmlMalloc = *(xmlMallocFunc *)p;
    if ((p = libxml2_sym("xmlMallocAtomic")) != NULL)  xmlMallocAtomic = *(xmlMallocFunc *)p;
    if ((p = libxml2_sym("xmlRealloc")) != NULL)       xmlRealloc = *(xmlReallocFunc *)p;
    if ((p = libxml2_sym("xmlMemStrdup")) != NULL)     xmlMemStrdup = *(xmlStrdupFunc *)p;
    if ((p = libxml2_sym("xmlParserVersion")) != NULL) xmlParserVersion = *(const char **)p;

    if ((p = libxml2_sym("xmlStringText")) != NULL)
        snprintf((char *)xmlStringText, sizeof xmlStringText, "%s", (const char *)p);
    if ((p = libxml2_sym("xmlStringTextNoenc")) != NULL)
        snprintf((char *)xmlStringTextNoenc, sizeof xmlStringTextNoenc, "%s", (const char *)p);
    if ((p = libxml2_sym("xmlStringComment")) != NULL)
        snprintf((char *)xmlStringComment, sizeof xmlStringComment, "%s", (const char *)p);

    if ((p = libxml2_sym("xmlIsPubidChar_tab")) != NULL)
        memcpy(xmlIsPubidChar_tab, p, sizeof xmlIsPubidChar_tab);

    if ((p = libxml2_sym("xmlDefaultSAXHandler")) != NULL)
        memcpy(&xmlDefaultSAXHandler, p, sizeof xmlDefaultSAXHandler);
    if ((p = libxml2_sym("htmlDefaultSAXHandler")) != NULL)
        memcpy(&htmlDefaultSAXHandler, p, sizeof htmlDefaultSAXHandler);
    if ((p = libxml2_sym("xmlDefaultSAXLocator")) != NULL)
        memcpy(&xmlDefaultSAXLocator, p, sizeof xmlDefaultSAXLocator);

    if ((p = libxml2_sym("xmlIsBaseCharGroup")) != NULL)
        memcpy(&xmlIsBaseCharGroup, p, sizeof xmlIsBaseCharGroup);
    if ((p = libxml2_sym("xmlIsCharGroup")) != NULL)
        memcpy(&xmlIsCharGroup, p, sizeof xmlIsCharGroup);
    if ((p = libxml2_sym("xmlIsCombiningGroup")) != NULL)
        memcpy(&xmlIsCombiningGroup, p, sizeof xmlIsCombiningGroup);
    if ((p = libxml2_sym("xmlIsDigitGroup")) != NULL)
        memcpy(&xmlIsDigitGroup, p, sizeof xmlIsDigitGroup);
    if ((p = libxml2_sym("xmlIsExtenderGroup")) != NULL)
        memcpy(&xmlIsExtenderGroup, p, sizeof xmlIsExtenderGroup);
    if ((p = libxml2_sym("xmlIsIdeographicGroup")) != NULL)
        memcpy(&xmlIsIdeographicGroup, p, sizeof xmlIsIdeographicGroup);

    if ((p = libxml2_sym("xmlXPathNAN")) != NULL)  xmlXPathNAN = *(double *)p;
    if ((p = libxml2_sym("xmlXPathPINF")) != NULL) xmlXPathPINF = *(double *)p;
    if ((p = libxml2_sym("xmlXPathNINF")) != NULL) xmlXPathNINF = *(double *)p;
}


/* ============================================================
 * WRAP 宏（首次调用 dlsym 并缓存；失败打印 stderr 返回安全值）
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!libxml2_dyn_libok()) return fallback;                          \
        *(void **)(&fn) = libxml2_sym(#name);                               \
        if (!fn) {                                                        \
            fprintf(stderr, "libxml2_dyn: dlsym failed: %s\n", #name);     \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define WRAP_VOID(name, params, args)                                     \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!libxml2_dyn_libok()) return;                                   \
        *(void **)(&fn) = libxml2_sym(#name);                               \
        if (!fn) {                                                        \
            fprintf(stderr, "libxml2_dyn: dlsym failed: %s\n", #name);     \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}

/* ============================================================
 * 可变参数转发辅助
 *   C 语言无法把 ... 原样转发给 dlsym 得到的函数指针，因此先把
 *   参数格式化成堆缓冲，再以 "%s" 作为格式串调用真实变参函数。
 *   值本身可能含 '%'，但它是作为参数传入而不是格式串，安全。
 * ============================================================ */
static char *libxml2_vformat(const char *fmt, va_list ap) {
    va_list ap2;
    int n;
    char *b;
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) return NULL;
    b = (char *)malloc((size_t)n + 1);
    if (!b) return NULL;
    vsnprintf(b, (size_t)n + 1, fmt, ap);
    return b;
}

/* fdecl: 带参数名的完整形参表（含 ...）
 * fptr:  函数指针所需的形参类型表（不含 ...）
 * fargs: 转发实参（格式化结果以 "%s", b 传入）
 * fmtarg:给 va_start 用的最后一个具名参数 */
#define VFWD_INT(name, fdecl, fptr, fargs, fmtarg)                        \
int name fdecl {                                                          \
    static __typeof__(name) *fn = NULL;                                   \
    va_list ap;                                                           \
    char *b;                                                              \
    int r;                                                                \
    if (!fn) {                                                            \
        if (!libxml2_dyn_libok()) return -1;                              \
        *(void **)(&fn) = libxml2_sym(#name);                             \
        if (!fn) {                                                        \
            fprintf(stderr, "libxml2_dyn: dlsym failed: %s\n", #name);    \
            return -1;                                                    \
        }                                                                 \
    }                                                                     \
    va_start(ap, fmtarg);                                                 \
    b = libxml2_vformat(fmtarg, ap);                                      \
    va_end(ap);                                                           \
    if (!b) return -1;                                                    \
    r = fn fargs;                                                         \
    free(b);                                                              \
    return r;                                                             \
}

#define VFWD_VOID(name, fdecl, fptr, fargs, fmtarg)                       \
void name fdecl {                                                         \
    static __typeof__(name) *fn = NULL;                                   \
    va_list ap;                                                           \
    char *b;                                                              \
    if (!fn) {                                                            \
        if (!libxml2_dyn_libok()) return;                                 \
        *(void **)(&fn) = libxml2_sym(#name);                             \
        if (!fn) {                                                        \
            fprintf(stderr, "libxml2_dyn: dlsym failed: %s\n", #name);    \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    va_start(ap, fmtarg);                                                 \
    b = libxml2_vformat(fmtarg, ap);                                      \
    va_end(ap);                                                           \
    if (!b) return;                                                       \
    fn fargs;                                                             \
    free(b);                                                              \
}

/* ============================================================
 * 导出函数封装（与 nm -D 导出一一对应，共 1400 个）
 * ============================================================ */
WRAP_RET(xmlDeregisterNodeFunc *, __xmlDeregisterNodeDefaultValue, (void), (), NULL)
WRAP_RET(int *, __xmlDoValidityCheckingDefaultValue, (void), (), NULL)
WRAP_RET(xmlGenericErrorFunc *, __xmlGenericError, (void), (), NULL)
WRAP_RET(void **, __xmlGenericErrorContext, (void), (), NULL)
WRAP_RET(int *, __xmlGetWarningsDefaultValue, (void), (), NULL)
WRAP_RET(int *, __xmlIndentTreeOutput, (void), (), NULL)
WRAP_RET(int *, __xmlKeepBlanksDefaultValue, (void), (), NULL)
WRAP_RET(const xmlError *, __xmlLastError, (void), (), NULL)
WRAP_RET(int *, __xmlLineNumbersDefaultValue, (void), (), NULL)
WRAP_RET(int *, __xmlLoadExtDtdDefaultValue, (void), (), NULL)
WRAP_RET(xmlOutputBuffer *, __xmlOutputBufferCreateFilename, (const char * a0, xmlCharEncodingHandler * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlOutputBufferCreateFilenameFunc *, __xmlOutputBufferCreateFilenameValue, (void), (), NULL)
WRAP_RET(xmlParserInputBuffer *, __xmlParserInputBufferCreateFilename, (const char * a0, xmlCharEncoding a1), (a0, a1), NULL)
WRAP_RET(xmlParserInputBufferCreateFilenameFunc *, __xmlParserInputBufferCreateFilenameValue, (void), (), NULL)
WRAP_RET(int *, __xmlPedanticParserDefaultValue, (void), (), NULL)
WRAP_RET(xmlRegisterNodeFunc *, __xmlRegisterNodeDefaultValue, (void), (), NULL)
WRAP_RET(int *, __xmlSaveNoEmptyTags, (void), (), NULL)
WRAP_RET(xmlStructuredErrorFunc *, __xmlStructuredError, (void), (), NULL)
WRAP_RET(void **, __xmlStructuredErrorContext, (void), (), NULL)
WRAP_RET(int *, __xmlSubstituteEntitiesDefaultValue, (void), (), NULL)
WRAP_RET(const char **, __xmlTreeIndentString, (void), (), NULL)
WRAP_RET(htmlStatus, htmlAttrAllowed, (const htmlElemDesc * a0, const xmlChar * a1, int a2), (a0, a1, a2), ((htmlStatus){0}))
WRAP_RET(int, htmlAutoCloseTag, (xmlDoc * a0, const xmlChar * a1, xmlNode * a2), (a0, a1, a2), -1)
WRAP_RET(htmlParserCtxt *, htmlCreateFileParserCtxt, (const char * a0, const char * a1), (a0, a1), NULL)
WRAP_RET(htmlParserCtxt *, htmlCreateMemoryParserCtxt, (const char * a0, int a1), (a0, a1), NULL)
WRAP_RET(htmlParserCtxt *, htmlCreatePushParserCtxt, (htmlSAXHandler * a0, void * a1, const char * a2, int a3, const char * a4, xmlCharEncoding a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(xmlDoc *, htmlCtxtParseDocument, (htmlParserCtxt * a0, xmlParserInput * a1), (a0, a1), NULL)
WRAP_RET(xmlDoc *, htmlCtxtReadDoc, (xmlParserCtxt * a0, const xmlChar * a1, const char * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(xmlDoc *, htmlCtxtReadFd, (xmlParserCtxt * a0, int a1, const char * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(xmlDoc *, htmlCtxtReadFile, (xmlParserCtxt * a0, const char * a1, const char * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlDoc *, htmlCtxtReadIO, (xmlParserCtxt * a0, xmlInputReadCallback a1, xmlInputCloseCallback a2, void * a3, const char * a4, const char * a5, int a6), (a0, a1, a2, a3, a4, a5, a6), NULL)
WRAP_RET(xmlDoc *, htmlCtxtReadMemory, (xmlParserCtxt * a0, const char * a1, int a2, const char * a3, const char * a4, int a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_VOID(htmlCtxtReset, (htmlParserCtxt * a0), (a0))
WRAP_RET(int, htmlCtxtSetOptions, (htmlParserCtxt * a0, int a1), (a0, a1), -1)
WRAP_RET(int, htmlCtxtUseOptions, (htmlParserCtxt * a0, int a1), (a0, a1), -1)
WRAP_VOID(htmlDefaultSAXHandlerInit, (void), ())
WRAP_VOID(htmlDocContentDumpFormatOutput, (xmlOutputBuffer * a0, xmlDoc * a1, const char * a2, int a3), (a0, a1, a2, a3))
WRAP_VOID(htmlDocContentDumpOutput, (xmlOutputBuffer * a0, xmlDoc * a1, const char * a2), (a0, a1, a2))
WRAP_RET(int, htmlDocDump, (FILE * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_VOID(htmlDocDumpMemory, (xmlDoc * a0, xmlChar ** a1, int * a2), (a0, a1, a2))
WRAP_VOID(htmlDocDumpMemoryFormat, (xmlDoc * a0, xmlChar ** a1, int * a2, int a3), (a0, a1, a2, a3))
WRAP_RET(int, htmlElementAllowedHere, (const htmlElemDesc * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(htmlStatus, htmlElementStatusHere, (const htmlElemDesc * a0, const htmlElemDesc * a1), (a0, a1), ((htmlStatus){0}))
WRAP_RET(int, htmlEncodeEntities, (unsigned char * a0, int * a1, const unsigned char * a2, int * a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(const htmlEntityDesc *, htmlEntityLookup, (const xmlChar * a0), (a0), NULL)
WRAP_RET(const htmlEntityDesc *, htmlEntityValueLookup, (unsigned int a0), (a0), NULL)
WRAP_VOID(htmlFreeParserCtxt, (htmlParserCtxt * a0), (a0))
WRAP_RET(const xmlChar *, htmlGetMetaEncoding, (xmlDoc * a0), (a0), NULL)
WRAP_RET(int, htmlHandleOmittedElem, (int a0), (a0), -1)
WRAP_VOID(htmlInitAutoClose, (void), ())
WRAP_RET(int, htmlIsAutoClosed, (xmlDoc * a0, xmlNode * a1), (a0, a1), -1)
WRAP_RET(int, htmlIsBooleanAttr, (const xmlChar * a0), (a0), -1)
WRAP_RET(int, htmlIsScriptAttribute, (const xmlChar * a0), (a0), -1)
WRAP_RET(xmlDoc *, htmlNewDoc, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlDoc *, htmlNewDocNoDtD, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(htmlParserCtxt *, htmlNewParserCtxt, (void), (), NULL)
WRAP_RET(htmlParserCtxt *, htmlNewSAXParserCtxt, (const htmlSAXHandler * a0, void * a1), (a0, a1), NULL)
WRAP_RET(int, htmlNodeDump, (xmlBuffer * a0, xmlDoc * a1, xmlNode * a2), (a0, a1, a2), -1)
WRAP_VOID(htmlNodeDumpFile, (FILE * a0, xmlDoc * a1, xmlNode * a2), (a0, a1, a2))
WRAP_RET(int, htmlNodeDumpFileFormat, (FILE * a0, xmlDoc * a1, xmlNode * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_VOID(htmlNodeDumpFormatOutput, (xmlOutputBuffer * a0, xmlDoc * a1, xmlNode * a2, const char * a3, int a4), (a0, a1, a2, a3, a4))
WRAP_VOID(htmlNodeDumpOutput, (xmlOutputBuffer * a0, xmlDoc * a1, xmlNode * a2, const char * a3), (a0, a1, a2, a3))
WRAP_RET(htmlStatus, htmlNodeStatus, (xmlNode * a0, int a1), (a0, a1), ((htmlStatus){0}))
WRAP_RET(int, htmlParseCharRef, (htmlParserCtxt * a0), (a0), -1)
WRAP_RET(int, htmlParseChunk, (htmlParserCtxt * a0, const char * a1, int a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(xmlDoc *, htmlParseDoc, (const xmlChar * a0, const char * a1), (a0, a1), NULL)
WRAP_RET(int, htmlParseDocument, (htmlParserCtxt * a0), (a0), -1)
WRAP_VOID(htmlParseElement, (htmlParserCtxt * a0), (a0))
WRAP_RET(const htmlEntityDesc *, htmlParseEntityRef, (htmlParserCtxt * a0, const xmlChar ** a1), (a0, a1), NULL)
WRAP_RET(xmlDoc *, htmlParseFile, (const char * a0, const char * a1), (a0, a1), NULL)
WRAP_RET(xmlDoc *, htmlReadDoc, (const xmlChar * a0, const char * a1, const char * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlDoc *, htmlReadFd, (int a0, const char * a1, const char * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlDoc *, htmlReadFile, (const char * a0, const char * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlDoc *, htmlReadIO, (xmlInputReadCallback a0, xmlInputCloseCallback a1, void * a2, const char * a3, const char * a4, int a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(xmlDoc *, htmlReadMemory, (const char * a0, int a1, const char * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(xmlDoc *, htmlSAXParseDoc, (const xmlChar * a0, const char * a1, htmlSAXHandler * a2, void * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlDoc *, htmlSAXParseFile, (const char * a0, const char * a1, htmlSAXHandler * a2, void * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(int, htmlSaveFile, (const char * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_RET(int, htmlSaveFileEnc, (const char * a0, xmlDoc * a1, const char * a2), (a0, a1, a2), -1)
WRAP_RET(int, htmlSaveFileFormat, (const char * a0, xmlDoc * a1, const char * a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, htmlSetMetaEncoding, (xmlDoc * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(const htmlElemDesc *, htmlTagLookup, (const xmlChar * a0), (a0), NULL)
WRAP_RET(int, htmlUTF8ToHtml, (unsigned char * a0, int * a1, const unsigned char * a2, int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(xlinkNodeDetectFunc, xlinkGetDefaultDetect, (void), (), ((xlinkNodeDetectFunc){0}))
WRAP_RET(xlinkHandler *, xlinkGetDefaultHandler, (void), (), NULL)
WRAP_RET(xlinkType, xlinkIsLink, (xmlDoc * a0, xmlNode * a1), (a0, a1), ((xlinkType){0}))
WRAP_VOID(xlinkSetDefaultDetect, (xlinkNodeDetectFunc a0), (a0))
WRAP_VOID(xlinkSetDefaultHandler, (xlinkHandler * a0), (a0))
WRAP_RET(int, xmlACatalogAdd, (xmlCatalog * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), -1)
WRAP_VOID(xmlACatalogDump, (xmlCatalog * a0, FILE * a1), (a0, a1))
WRAP_RET(int, xmlACatalogRemove, (xmlCatalog * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(xmlChar *, xmlACatalogResolve, (xmlCatalog * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlChar *, xmlACatalogResolvePublic, (xmlCatalog * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlChar *, xmlACatalogResolveSystem, (xmlCatalog * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlChar *, xmlACatalogResolveURI, (xmlCatalog * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlAttribute *, xmlAddAttributeDecl, (xmlValidCtxt * a0, xmlDtd * a1, const xmlChar * a2, const xmlChar * a3, const xmlChar * a4, xmlAttributeType a5, xmlAttributeDefault a6, const xmlChar * a7, xmlEnumeration * a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), NULL)
WRAP_RET(xmlNode *, xmlAddChild, (xmlNode * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlAddChildList, (xmlNode * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlEntity *, xmlAddDocEntity, (xmlDoc * a0, const xmlChar * a1, int a2, const xmlChar * a3, const xmlChar * a4, const xmlChar * a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(xmlEntity *, xmlAddDtdEntity, (xmlDoc * a0, const xmlChar * a1, int a2, const xmlChar * a3, const xmlChar * a4, const xmlChar * a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(xmlElement *, xmlAddElementDecl, (xmlValidCtxt * a0, xmlDtd * a1, const xmlChar * a2, xmlElementTypeVal a3, xmlElementContent * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(int, xmlAddEncodingAlias, (const char * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, xmlAddEntity, (xmlDoc * a0, int a1, const xmlChar * a2, int a3, const xmlChar * a4, const xmlChar * a5, const xmlChar * a6, xmlEntity ** a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(xmlID *, xmlAddID, (xmlValidCtxt * a0, xmlDoc * a1, const xmlChar * a2, xmlAttr * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(int, xmlAddIDSafe, (xmlAttr * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(xmlNode *, xmlAddNextSibling, (xmlNode * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNotation *, xmlAddNotationDecl, (xmlValidCtxt * a0, xmlDtd * a1, const xmlChar * a2, const xmlChar * a3, const xmlChar * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(xmlNode *, xmlAddPrevSibling, (xmlNode * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlRef *, xmlAddRef, (xmlValidCtxt * a0, xmlDoc * a1, const xmlChar * a2, xmlAttr * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlNode *, xmlAddSibling, (xmlNode * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlOutputBuffer *, xmlAllocOutputBuffer, (xmlCharEncodingHandler * a0), (a0), NULL)
WRAP_RET(xmlParserInputBuffer *, xmlAllocParserInputBuffer, (xmlCharEncoding a0), (a0), NULL)
WRAP_VOID(xmlAttrSerializeTxtContent, (xmlBuffer * a0, xmlDoc * a1, xmlAttr * a2, const xmlChar * a3), (a0, a1, a2, a3))
WRAP_RET(struct _xmlRegexp *, xmlAutomataCompile, (xmlAutomata * a0), (a0), NULL)
WRAP_RET(xmlAutomataState *, xmlAutomataGetInitState, (xmlAutomata * a0), (a0), NULL)
WRAP_RET(int, xmlAutomataIsDeterminist, (xmlAutomata * a0), (a0), -1)
WRAP_RET(xmlAutomataState *, xmlAutomataNewAllTrans, (xmlAutomata * a0, xmlAutomataState * a1, xmlAutomataState * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlAutomataState *, xmlAutomataNewCountTrans, (xmlAutomata * a0, xmlAutomataState * a1, xmlAutomataState * a2, const xmlChar * a3, int a4, int a5, void * a6), (a0, a1, a2, a3, a4, a5, a6), NULL)
WRAP_RET(xmlAutomataState *, xmlAutomataNewCountTrans2, (xmlAutomata * a0, xmlAutomataState * a1, xmlAutomataState * a2, const xmlChar * a3, const xmlChar * a4, int a5, int a6, void * a7), (a0, a1, a2, a3, a4, a5, a6, a7), NULL)
WRAP_RET(xmlAutomataState *, xmlAutomataNewCountedTrans, (xmlAutomata * a0, xmlAutomataState * a1, xmlAutomataState * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(int, xmlAutomataNewCounter, (xmlAutomata * a0, int a1, int a2), (a0, a1, a2), -1)
WRAP_RET(xmlAutomataState *, xmlAutomataNewCounterTrans, (xmlAutomata * a0, xmlAutomataState * a1, xmlAutomataState * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlAutomataState *, xmlAutomataNewEpsilon, (xmlAutomata * a0, xmlAutomataState * a1, xmlAutomataState * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlAutomataState *, xmlAutomataNewNegTrans, (xmlAutomata * a0, xmlAutomataState * a1, xmlAutomataState * a2, const xmlChar * a3, const xmlChar * a4, void * a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(xmlAutomataState *, xmlAutomataNewOnceTrans, (xmlAutomata * a0, xmlAutomataState * a1, xmlAutomataState * a2, const xmlChar * a3, int a4, int a5, void * a6), (a0, a1, a2, a3, a4, a5, a6), NULL)
WRAP_RET(xmlAutomataState *, xmlAutomataNewOnceTrans2, (xmlAutomata * a0, xmlAutomataState * a1, xmlAutomataState * a2, const xmlChar * a3, const xmlChar * a4, int a5, int a6, void * a7), (a0, a1, a2, a3, a4, a5, a6, a7), NULL)
WRAP_RET(xmlAutomataState *, xmlAutomataNewState, (xmlAutomata * a0), (a0), NULL)
WRAP_RET(xmlAutomataState *, xmlAutomataNewTransition, (xmlAutomata * a0, xmlAutomataState * a1, xmlAutomataState * a2, const xmlChar * a3, void * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(xmlAutomataState *, xmlAutomataNewTransition2, (xmlAutomata * a0, xmlAutomataState * a1, xmlAutomataState * a2, const xmlChar * a3, const xmlChar * a4, void * a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(int, xmlAutomataSetFinalState, (xmlAutomata * a0, xmlAutomataState * a1), (a0, a1), -1)
WRAP_RET(xmlChar *, xmlBufContent, (const xmlBuf * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlBufEnd, (xmlBuf * a0), (a0), NULL)
WRAP_RET(int, xmlBufGetNodeContent, (xmlBuf * a0, const xmlNode * a1), (a0, a1), -1)
WRAP_RET(size_t, xmlBufNodeDump, (xmlBuf * a0, xmlDoc * a1, xmlNode * a2, int a3, int a4), (a0, a1, a2, a3, a4), ((size_t){0}))
WRAP_RET(size_t, xmlBufShrink, (xmlBuf * a0, size_t a1), (a0, a1), ((size_t){0}))
WRAP_RET(size_t, xmlBufUse, (xmlBuf * a0), (a0), ((size_t){0}))
WRAP_RET(int, xmlBufferAdd, (xmlBuffer * a0, const xmlChar * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlBufferAddHead, (xmlBuffer * a0, const xmlChar * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlBufferCCat, (xmlBuffer * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, xmlBufferCat, (xmlBuffer * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(const xmlChar *, xmlBufferContent, (const xmlBuffer * a0), (a0), NULL)
WRAP_RET(xmlBuffer *, xmlBufferCreate, (void), (), NULL)
WRAP_RET(xmlBuffer *, xmlBufferCreateSize, (size_t a0), (a0), NULL)
WRAP_RET(xmlBuffer *, xmlBufferCreateStatic, (void * a0, size_t a1), (a0, a1), NULL)
WRAP_RET(xmlChar *, xmlBufferDetach, (xmlBuffer * a0), (a0), NULL)
WRAP_RET(int, xmlBufferDump, (FILE * a0, xmlBuffer * a1), (a0, a1), -1)
WRAP_VOID(xmlBufferEmpty, (xmlBuffer * a0), (a0))
WRAP_VOID(xmlBufferFree, (xmlBuffer * a0), (a0))
WRAP_RET(int, xmlBufferGrow, (xmlBuffer * a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, xmlBufferLength, (const xmlBuffer * a0), (a0), -1)
WRAP_RET(int, xmlBufferResize, (xmlBuffer * a0, unsigned int a1), (a0, a1), -1)
WRAP_VOID(xmlBufferSetAllocationScheme, (xmlBuffer * a0, xmlBufferAllocationScheme a1), (a0, a1))
WRAP_RET(int, xmlBufferShrink, (xmlBuffer * a0, unsigned int a1), (a0, a1), -1)
WRAP_VOID(xmlBufferWriteCHAR, (xmlBuffer * a0, const xmlChar * a1), (a0, a1))
WRAP_VOID(xmlBufferWriteChar, (xmlBuffer * a0, const char * a1), (a0, a1))
WRAP_VOID(xmlBufferWriteQuotedString, (xmlBuffer * a0, const xmlChar * a1), (a0, a1))
WRAP_RET(xmlChar *, xmlBuildQName, (const xmlChar * a0, const xmlChar * a1, xmlChar * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlChar *, xmlBuildRelativeURI, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(int, xmlBuildRelativeURISafe, (const xmlChar * a0, const xmlChar * a1, xmlChar ** a2), (a0, a1, a2), -1)
WRAP_RET(xmlChar *, xmlBuildURI, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(int, xmlBuildURISafe, (const xmlChar * a0, const xmlChar * a1, xmlChar ** a2), (a0, a1, a2), -1)
WRAP_RET(long int, xmlByteConsumed, (xmlParserCtxt * a0), (a0), ((long int){0}))
WRAP_RET(int, xmlC14NDocDumpMemory, (xmlDoc * a0, xmlNodeSet * a1, int a2, xmlChar ** a3, int a4, xmlChar ** a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlC14NDocSave, (xmlDoc * a0, xmlNodeSet * a1, int a2, xmlChar ** a3, int a4, const char * a5, int a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, xmlC14NDocSaveTo, (xmlDoc * a0, xmlNodeSet * a1, int a2, xmlChar ** a3, int a4, xmlOutputBuffer * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlC14NExecute, (xmlDoc * a0, xmlC14NIsVisibleCallback a1, void * a2, int a3, xmlChar ** a4, int a5, xmlOutputBuffer * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(xmlChar *, xmlCanonicPath, (const xmlChar * a0), (a0), NULL)
WRAP_RET(int, xmlCatalogAdd, (const xmlChar * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(void *, xmlCatalogAddLocal, (void * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_VOID(xmlCatalogCleanup, (void), ())
WRAP_RET(int, xmlCatalogConvert, (void), (), -1)
WRAP_VOID(xmlCatalogDump, (FILE * a0), (a0))
WRAP_RET(xmlDocPtr, xmlCatalogDumpDoc, (void), (), ((xmlDocPtr){0}))
WRAP_VOID(xmlCatalogFreeLocal, (void * a0), (a0))
WRAP_RET(xmlCatalogAllow, xmlCatalogGetDefaults, (void), (), ((xmlCatalogAllow){0}))
WRAP_RET(const xmlChar *, xmlCatalogGetPublic, (const xmlChar * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlCatalogGetSystem, (const xmlChar * a0), (a0), NULL)
WRAP_RET(int, xmlCatalogIsEmpty, (xmlCatalog * a0), (a0), -1)
WRAP_RET(xmlChar *, xmlCatalogLocalResolve, (void * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlChar *, xmlCatalogLocalResolveURI, (void * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(int, xmlCatalogRemove, (const xmlChar * a0), (a0), -1)
WRAP_RET(xmlChar *, xmlCatalogResolve, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlChar *, xmlCatalogResolvePublic, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlCatalogResolveSystem, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlCatalogResolveURI, (const xmlChar * a0), (a0), NULL)
WRAP_RET(int, xmlCatalogSetDebug, (int a0), (a0), -1)
WRAP_RET(xmlCatalogPrefer, xmlCatalogSetDefaultPrefer, (xmlCatalogPrefer a0), (a0), ((xmlCatalogPrefer){0}))
WRAP_VOID(xmlCatalogSetDefaults, (xmlCatalogAllow a0), (a0))
WRAP_RET(int, xmlCharEncCloseFunc, (xmlCharEncodingHandler * a0), (a0), -1)
WRAP_RET(int, xmlCharEncFirstLine, (xmlCharEncodingHandler * a0, struct _xmlBuffer * a1, struct _xmlBuffer * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlCharEncInFunc, (xmlCharEncodingHandler * a0, struct _xmlBuffer * a1, struct _xmlBuffer * a2), (a0, a1, a2), -1)
WRAP_RET(xmlParserErrors, xmlCharEncNewCustomHandler, (const char * a0, xmlCharEncConvFunc a1, xmlCharEncConvFunc a2, xmlCharEncConvCtxtDtor a3, void * a4, void * a5, xmlCharEncodingHandler ** a6), (a0, a1, a2, a3, a4, a5, a6), ((xmlParserErrors){0}))
WRAP_RET(int, xmlCharEncOutFunc, (xmlCharEncodingHandler * a0, struct _xmlBuffer * a1, struct _xmlBuffer * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlCharInRange, (unsigned int a0, const xmlChRangeGroup * a1), (a0, a1), -1)
WRAP_RET(xmlChar *, xmlCharStrdup, (const char * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlCharStrndup, (const char * a0, int a1), (a0, a1), NULL)
WRAP_RET(int, xmlCheckFilename, (const char * a0), (a0), -1)
WRAP_RET(xmlParserInput *, xmlCheckHTTPInput, (xmlParserCtxt * a0, xmlParserInput * a1), (a0, a1), NULL)
WRAP_RET(int, xmlCheckLanguageID, (const xmlChar * a0), (a0), -1)
WRAP_RET(int, xmlCheckThreadLocalStorage, (void), (), -1)
WRAP_RET(int, xmlCheckUTF8, (const unsigned char * a0), (a0), -1)
WRAP_VOID(xmlCheckVersion, (int a0), (a0))
WRAP_RET(long unsigned int, xmlChildElementCount, (xmlNode * a0), (a0), ((long unsigned int){0}))
WRAP_VOID(xmlCleanupCharEncodingHandlers, (void), ())
WRAP_VOID(xmlCleanupEncodingAliases, (void), ())
WRAP_VOID(xmlCleanupGlobals, (void), ())
WRAP_VOID(xmlCleanupInputCallbacks, (void), ())
WRAP_VOID(xmlCleanupMemory, (void), ())
WRAP_VOID(xmlCleanupOutputCallbacks, (void), ())
WRAP_VOID(xmlCleanupParser, (void), ())
WRAP_VOID(xmlCleanupThreads, (void), ())
WRAP_VOID(xmlClearNodeInfoSeq, (xmlParserNodeInfoSeq * a0), (a0))
WRAP_VOID(xmlClearParserCtxt, (xmlParserCtxt * a0), (a0))
WRAP_RET(int, xmlConvertSGMLCatalog, (xmlCatalog * a0), (a0), -1)
WRAP_RET(xmlAttributeTable *, xmlCopyAttributeTable, (xmlAttributeTable * a0), (a0), NULL)
WRAP_RET(int, xmlCopyChar, (int a0, xmlChar * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlCopyCharMultiByte, (xmlChar * a0, int a1), (a0, a1), -1)
WRAP_RET(xmlDoc *, xmlCopyDoc, (xmlDoc * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlElementContent *, xmlCopyDocElementContent, (xmlDoc * a0, xmlElementContent * a1), (a0, a1), NULL)
WRAP_RET(xmlDtd *, xmlCopyDtd, (xmlDtd * a0), (a0), NULL)
WRAP_RET(xmlElementContent *, xmlCopyElementContent, (xmlElementContent * a0), (a0), NULL)
WRAP_RET(xmlElementTable *, xmlCopyElementTable, (xmlElementTable * a0), (a0), NULL)
WRAP_RET(xmlEntitiesTable *, xmlCopyEntitiesTable, (xmlEntitiesTable * a0), (a0), NULL)
WRAP_RET(xmlEnumeration *, xmlCopyEnumeration, (xmlEnumeration * a0), (a0), NULL)
WRAP_RET(int, xmlCopyError, (const xmlError * a0, xmlError * a1), (a0, a1), -1)
WRAP_RET(xmlNs *, xmlCopyNamespace, (xmlNs * a0), (a0), NULL)
WRAP_RET(xmlNs *, xmlCopyNamespaceList, (xmlNs * a0), (a0), NULL)
WRAP_RET(xmlNode *, xmlCopyNode, (xmlNode * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlCopyNodeList, (xmlNode * a0), (a0), NULL)
WRAP_RET(xmlNotationTable *, xmlCopyNotationTable, (xmlNotationTable * a0), (a0), NULL)
WRAP_RET(xmlAttr *, xmlCopyProp, (xmlNode * a0, xmlAttr * a1), (a0, a1), NULL)
WRAP_RET(xmlAttr *, xmlCopyPropList, (xmlNode * a0, xmlAttr * a1), (a0, a1), NULL)
WRAP_RET(xmlParserErrors, xmlCreateCharEncodingHandler, (const char * a0, xmlCharEncFlags a1, xmlCharEncConvImpl a2, void * a3, xmlCharEncodingHandler ** a4), (a0, a1, a2, a3, a4), ((xmlParserErrors){0}))
WRAP_RET(xmlParserCtxt *, xmlCreateDocParserCtxt, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlEntitiesTable *, xmlCreateEntitiesTable, (void), (), NULL)
WRAP_RET(xmlParserCtxt *, xmlCreateEntityParserCtxt, (const xmlChar * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlEnumeration *, xmlCreateEnumeration, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlParserCtxt *, xmlCreateFileParserCtxt, (const char * a0), (a0), NULL)
WRAP_RET(xmlParserCtxt *, xmlCreateIOParserCtxt, (xmlSAXHandler * a0, void * a1, xmlInputReadCallback a2, xmlInputCloseCallback a3, void * a4, xmlCharEncoding a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(xmlDtd *, xmlCreateIntSubset, (xmlDoc * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlParserCtxt *, xmlCreateMemoryParserCtxt, (const char * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlParserCtxt *, xmlCreatePushParserCtxt, (xmlSAXHandler * a0, void * a1, const char * a2, int a3, const char * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(xmlURI *, xmlCreateURI, (void), (), NULL)
WRAP_RET(xmlParserCtxt *, xmlCreateURLParserCtxt, (const char * a0, int a1), (a0, a1), NULL)
WRAP_VOID(xmlCtxtErrMemory, (xmlParserCtxt * a0), (a0))
WRAP_RET(void *, xmlCtxtGetCatalogs, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlCtxtGetDeclaredEncoding, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(xmlDict *, xmlCtxtGetDict, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(int, xmlCtxtGetDocTypeDecl, (xmlParserCtxt * a0, const xmlChar ** a1, const xmlChar ** a2, const xmlChar ** a3), (a0, a1, a2, a3), -1)
WRAP_RET(xmlDoc *, xmlCtxtGetDocument, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(int, xmlCtxtGetInputPosition, (xmlParserCtxt * a0, int a1, const char ** a2, int * a3, int * a4, long unsigned int * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlCtxtGetInputWindow, (xmlParserCtxt * a0, int a1, const xmlChar ** a2, int * a3, int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(const xmlError *, xmlCtxtGetLastError, (void * a0), (a0), NULL)
WRAP_RET(xmlNode *, xmlCtxtGetNode, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(int, xmlCtxtGetOptions, (xmlParserCtxt * a0), (a0), -1)
WRAP_RET(void *, xmlCtxtGetPrivate, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(xmlSAXHandler *, xmlCtxtGetSaxHandler, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(int, xmlCtxtGetStandalone, (xmlParserCtxt * a0), (a0), -1)
WRAP_RET(xmlParserStatus, xmlCtxtGetStatus, (xmlParserCtxt * a0), (a0), ((xmlParserStatus){0}))
WRAP_RET(void *, xmlCtxtGetUserData, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(xmlValidCtxt *, xmlCtxtGetValidCtxt, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlCtxtGetVersion, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(int, xmlCtxtIsHtml, (xmlParserCtxt * a0), (a0), -1)
WRAP_RET(int, xmlCtxtIsInSubset, (xmlParserCtxt * a0), (a0), -1)
WRAP_RET(int, xmlCtxtIsStopped, (xmlParserCtxt * a0), (a0), -1)
WRAP_RET(xmlNode *, xmlCtxtParseContent, (xmlParserCtxt * a0, xmlParserInput * a1, xmlNode * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlDoc *, xmlCtxtParseDocument, (xmlParserCtxt * a0, xmlParserInput * a1), (a0, a1), NULL)
WRAP_RET(xmlDtd *, xmlCtxtParseDtd, (xmlParserCtxt * a0, xmlParserInput * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlParserInput *, xmlCtxtPopInput, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(int, xmlCtxtPushInput, (xmlParserCtxt * a0, xmlParserInput * a1), (a0, a1), -1)
WRAP_RET(xmlDoc *, xmlCtxtReadDoc, (xmlParserCtxt * a0, const xmlChar * a1, const char * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(xmlDoc *, xmlCtxtReadFd, (xmlParserCtxt * a0, int a1, const char * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(xmlDoc *, xmlCtxtReadFile, (xmlParserCtxt * a0, const char * a1, const char * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlDoc *, xmlCtxtReadIO, (xmlParserCtxt * a0, xmlInputReadCallback a1, xmlInputCloseCallback a2, void * a3, const char * a4, const char * a5, int a6), (a0, a1, a2, a3, a4, a5, a6), NULL)
WRAP_RET(xmlDoc *, xmlCtxtReadMemory, (xmlParserCtxt * a0, const char * a1, int a2, const char * a3, const char * a4, int a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_VOID(xmlCtxtReset, (xmlParserCtxt * a0), (a0))
WRAP_VOID(xmlCtxtResetLastError, (void * a0), (a0))
WRAP_RET(int, xmlCtxtResetPush, (xmlParserCtxt * a0, const char * a1, int a2, const char * a3, const char * a4), (a0, a1, a2, a3, a4), -1)
WRAP_VOID(xmlCtxtSetCatalogs, (xmlParserCtxt * a0, void * a1), (a0, a1))
WRAP_VOID(xmlCtxtSetCharEncConvImpl, (xmlParserCtxt * a0, xmlCharEncConvImpl a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlCtxtSetDict, (xmlParserCtxt * a0, xmlDict * a1), (a0, a1))
WRAP_VOID(xmlCtxtSetErrorHandler, (xmlParserCtxt * a0, xmlStructuredErrorFunc a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlCtxtSetMaxAmplification, (xmlParserCtxt * a0, unsigned int a1), (a0, a1))
WRAP_RET(int, xmlCtxtSetOptions, (xmlParserCtxt * a0, int a1), (a0, a1), -1)
WRAP_VOID(xmlCtxtSetPrivate, (xmlParserCtxt * a0, void * a1), (a0, a1))
WRAP_VOID(xmlCtxtSetResourceLoader, (xmlParserCtxt * a0, xmlResourceLoader a1, void * a2), (a0, a1, a2))
WRAP_RET(int, xmlCtxtSetSaxHandler, (xmlParserCtxt * a0, const xmlSAXHandler * a1), (a0, a1), -1)
WRAP_RET(int, xmlCtxtUseOptions, (xmlParserCtxt * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlCtxtValidateDocument, (xmlParserCtxt * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_RET(int, xmlCtxtValidateDtd, (xmlParserCtxt * a0, xmlDoc * a1, xmlDtd * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlCurrentChar, (xmlParserCtxt * a0, int * a1), (a0, a1), -1)
WRAP_RET(int, xmlDOMWrapAdoptNode, (xmlDOMWrapCtxt * a0, xmlDoc * a1, xmlNode * a2, xmlDoc * a3, xmlNode * a4, int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlDOMWrapCloneNode, (xmlDOMWrapCtxt * a0, xmlDoc * a1, xmlNode * a2, xmlNode ** a3, xmlDoc * a4, xmlNode * a5, int a6, int a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_VOID(xmlDOMWrapFreeCtxt, (xmlDOMWrapCtxt * a0), (a0))
WRAP_RET(xmlDOMWrapCtxt *, xmlDOMWrapNewCtxt, (void), (), NULL)
WRAP_RET(int, xmlDOMWrapReconcileNamespaces, (xmlDOMWrapCtxt * a0, xmlNode * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlDOMWrapRemoveNode, (xmlDOMWrapCtxt * a0, xmlDoc * a1, xmlNode * a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlDebugCheckDocument, (FILE * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_VOID(xmlDebugDumpAttr, (FILE * a0, xmlAttr * a1, int a2), (a0, a1, a2))
WRAP_VOID(xmlDebugDumpAttrList, (FILE * a0, xmlAttr * a1, int a2), (a0, a1, a2))
WRAP_VOID(xmlDebugDumpDTD, (FILE * a0, xmlDtd * a1), (a0, a1))
WRAP_VOID(xmlDebugDumpDocument, (FILE * a0, xmlDoc * a1), (a0, a1))
WRAP_VOID(xmlDebugDumpDocumentHead, (FILE * a0, xmlDoc * a1), (a0, a1))
WRAP_VOID(xmlDebugDumpEntities, (FILE * a0, xmlDoc * a1), (a0, a1))
WRAP_VOID(xmlDebugDumpNode, (FILE * a0, xmlNode * a1, int a2), (a0, a1, a2))
WRAP_VOID(xmlDebugDumpNodeList, (FILE * a0, xmlNode * a1, int a2), (a0, a1, a2))
WRAP_VOID(xmlDebugDumpOneNode, (FILE * a0, xmlNode * a1, int a2), (a0, a1, a2))
WRAP_VOID(xmlDebugDumpString, (FILE * a0, const xmlChar * a1), (a0, a1))
WRAP_VOID(xmlDefaultSAXHandlerInit, (void), ())
WRAP_RET(int, xmlDelEncodingAlias, (const char * a0), (a0), -1)
WRAP_RET(xmlDeregisterNodeFunc, xmlDeregisterNodeDefault, (xmlDeregisterNodeFunc a0), (a0), ((xmlDeregisterNodeFunc){0}))
WRAP_RET(xmlCharEncoding, xmlDetectCharEncoding, (const unsigned char * a0, int a1), (a0, a1), ((xmlCharEncoding){0}))
WRAP_VOID(xmlDictCleanup, (void), ())
WRAP_RET(xmlDict *, xmlDictCreate, (void), (), NULL)
WRAP_RET(xmlDict *, xmlDictCreateSub, (xmlDict * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlDictExists, (xmlDict * a0, const xmlChar * a1, int a2), (a0, a1, a2), NULL)
WRAP_VOID(xmlDictFree, (xmlDict * a0), (a0))
WRAP_RET(size_t, xmlDictGetUsage, (xmlDict * a0), (a0), ((size_t){0}))
WRAP_RET(const xmlChar *, xmlDictLookup, (xmlDict * a0, const xmlChar * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlDictOwns, (xmlDict * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(const xmlChar *, xmlDictQLookup, (xmlDict * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlDictReference, (xmlDict * a0), (a0), -1)
WRAP_RET(size_t, xmlDictSetLimit, (xmlDict * a0, size_t a1), (a0, a1), ((size_t){0}))
WRAP_RET(int, xmlDictSize, (xmlDict * a0), (a0), -1)
WRAP_RET(xmlNode *, xmlDocCopyNode, (xmlNode * a0, xmlDoc * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlNode *, xmlDocCopyNodeList, (xmlDoc * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(int, xmlDocDump, (FILE * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_VOID(xmlDocDumpFormatMemory, (xmlDoc * a0, xmlChar ** a1, int * a2, int a3), (a0, a1, a2, a3))
WRAP_VOID(xmlDocDumpFormatMemoryEnc, (xmlDoc * a0, xmlChar ** a1, int * a2, const char * a3, int a4), (a0, a1, a2, a3, a4))
WRAP_VOID(xmlDocDumpMemory, (xmlDoc * a0, xmlChar ** a1, int * a2), (a0, a1, a2))
WRAP_VOID(xmlDocDumpMemoryEnc, (xmlDoc * a0, xmlChar ** a1, int * a2, const char * a3), (a0, a1, a2, a3))
WRAP_RET(int, xmlDocFormatDump, (FILE * a0, xmlDoc * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(xmlNode *, xmlDocGetRootElement, (const xmlDoc * a0), (a0), NULL)
WRAP_RET(xmlNode *, xmlDocSetRootElement, (xmlDoc * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_VOID(xmlDumpAttributeDecl, (xmlBuffer * a0, xmlAttribute * a1), (a0, a1))
WRAP_VOID(xmlDumpAttributeTable, (xmlBuffer * a0, xmlAttributeTable * a1), (a0, a1))
WRAP_VOID(xmlDumpElementDecl, (xmlBuffer * a0, xmlElement * a1), (a0, a1))
WRAP_VOID(xmlDumpElementTable, (xmlBuffer * a0, xmlElementTable * a1), (a0, a1))
WRAP_VOID(xmlDumpEntitiesTable, (xmlBuffer * a0, xmlEntitiesTable * a1), (a0, a1))
WRAP_VOID(xmlDumpEntityDecl, (xmlBuffer * a0, xmlEntity * a1), (a0, a1))
WRAP_VOID(xmlDumpNotationDecl, (xmlBuffer * a0, xmlNotation * a1), (a0, a1))
WRAP_VOID(xmlDumpNotationTable, (xmlBuffer * a0, xmlNotationTable * a1), (a0, a1))
WRAP_VOID(xmlElemDump, (FILE * a0, xmlDoc * a1, xmlNode * a2), (a0, a1, a2))
WRAP_RET(xmlChar *, xmlEncodeEntitiesReentrant, (xmlDoc * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlChar *, xmlEncodeSpecialChars, (const xmlDoc * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(int, xmlFileClose, (void * a0), (a0), -1)
WRAP_RET(int, xmlFileMatch, (const char * a0), (a0), -1)
WRAP_RET(void *, xmlFileOpen, (const char * a0), (a0), NULL)
WRAP_RET(int, xmlFileRead, (void * a0, char * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(xmlCharEncodingHandler *, xmlFindCharEncodingHandler, (const char * a0), (a0), NULL)
WRAP_RET(xmlNode *, xmlFirstElementChild, (xmlNode * a0), (a0), NULL)
WRAP_VOID(xmlFormatError, (const xmlError * a0, xmlGenericErrorFunc a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlFreeAttributeTable, (xmlAttributeTable * a0), (a0))
WRAP_VOID(xmlFreeAutomata, (xmlAutomata * a0), (a0))
WRAP_VOID(xmlFreeCatalog, (xmlCatalog * a0), (a0))
WRAP_VOID(xmlFreeDoc, (xmlDoc * a0), (a0))
WRAP_VOID(xmlFreeDocElementContent, (xmlDoc * a0, xmlElementContent * a1), (a0, a1))
WRAP_VOID(xmlFreeDtd, (xmlDtd * a0), (a0))
WRAP_VOID(xmlFreeElementContent, (xmlElementContent * a0), (a0))
WRAP_VOID(xmlFreeElementTable, (xmlElementTable * a0), (a0))
WRAP_VOID(xmlFreeEntitiesTable, (xmlEntitiesTable * a0), (a0))
WRAP_VOID(xmlFreeEntity, (xmlEntity * a0), (a0))
WRAP_VOID(xmlFreeEnumeration, (xmlEnumeration * a0), (a0))
WRAP_VOID(xmlFreeIDTable, (xmlIDTable * a0), (a0))
WRAP_VOID(xmlFreeInputStream, (xmlParserInput * a0), (a0))
WRAP_VOID(xmlFreeMutex, (xmlMutex * a0), (a0))
WRAP_VOID(xmlFreeNode, (xmlNode * a0), (a0))
WRAP_VOID(xmlFreeNodeList, (xmlNode * a0), (a0))
WRAP_VOID(xmlFreeNotationTable, (xmlNotationTable * a0), (a0))
WRAP_VOID(xmlFreeNs, (xmlNs * a0), (a0))
WRAP_VOID(xmlFreeNsList, (xmlNs * a0), (a0))
WRAP_VOID(xmlFreeParserCtxt, (xmlParserCtxt * a0), (a0))
WRAP_VOID(xmlFreeParserInputBuffer, (xmlParserInputBuffer * a0), (a0))
WRAP_VOID(xmlFreePattern, (xmlPattern * a0), (a0))
WRAP_VOID(xmlFreePatternList, (xmlPattern * a0), (a0))
WRAP_VOID(xmlFreeProp, (xmlAttr * a0), (a0))
WRAP_VOID(xmlFreePropList, (xmlAttr * a0), (a0))
WRAP_VOID(xmlFreeRMutex, (xmlRMutex * a0), (a0))
WRAP_VOID(xmlFreeRefTable, (xmlRefTable * a0), (a0))
WRAP_VOID(xmlFreeStreamCtxt, (xmlStreamCtxt * a0), (a0))
WRAP_VOID(xmlFreeTextReader, (xmlTextReader * a0), (a0))
WRAP_VOID(xmlFreeTextWriter, (xmlTextWriter * a0), (a0))
WRAP_VOID(xmlFreeURI, (xmlURI * a0), (a0))
WRAP_VOID(xmlFreeValidCtxt, (xmlValidCtxt * a0), (a0))
WRAP_RET(int, xmlGcMemGet, (xmlFreeFunc * a0, xmlMallocFunc * a1, xmlMallocFunc * a2, xmlReallocFunc * a3, xmlStrdupFunc * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlGcMemSetup, (xmlFreeFunc a0, xmlMallocFunc a1, xmlMallocFunc a2, xmlReallocFunc a3, xmlStrdupFunc a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(xmlBufferAllocationScheme, xmlGetBufferAllocationScheme, (void), (), ((xmlBufferAllocationScheme){0}))
WRAP_RET(xmlCharEncodingHandler *, xmlGetCharEncodingHandler, (xmlCharEncoding a0), (a0), NULL)
WRAP_RET(const char *, xmlGetCharEncodingName, (xmlCharEncoding a0), (a0), NULL)
WRAP_RET(int, xmlGetCompressMode, (void), (), -1)
WRAP_RET(int, xmlGetDocCompressMode, (const xmlDoc * a0), (a0), -1)
WRAP_RET(xmlEntity *, xmlGetDocEntity, (const xmlDoc * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlAttribute *, xmlGetDtdAttrDesc, (xmlDtd * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlElement *, xmlGetDtdElementDesc, (xmlDtd * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlEntity *, xmlGetDtdEntity, (xmlDoc * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlNotation *, xmlGetDtdNotationDesc, (xmlDtd * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlAttribute *, xmlGetDtdQAttrDesc, (xmlDtd * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlElement *, xmlGetDtdQElementDesc, (xmlDtd * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(const char *, xmlGetEncodingAlias, (const char * a0), (a0), NULL)
WRAP_RET(xmlExternalEntityLoader, xmlGetExternalEntityLoader, (void), (), ((xmlExternalEntityLoader){0}))
WRAP_RET(xmlAttr *, xmlGetID, (xmlDoc * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlDtd *, xmlGetIntSubset, (const xmlDoc * a0), (a0), NULL)
WRAP_RET(xmlNode *, xmlGetLastChild, (const xmlNode * a0), (a0), NULL)
WRAP_RET(const xmlError *, xmlGetLastError, (void), (), NULL)
WRAP_RET(long int, xmlGetLineNo, (const xmlNode * a0), (a0), ((long int){0}))
WRAP_RET(xmlChar *, xmlGetNoNsProp, (const xmlNode * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlChar *, xmlGetNodePath, (const xmlNode * a0), (a0), NULL)
WRAP_RET(xmlNs **, xmlGetNsList, (const xmlDoc * a0, const xmlNode * a1), (a0, a1), NULL)
WRAP_RET(int, xmlGetNsListSafe, (const xmlDoc * a0, const xmlNode * a1, xmlNs *** a2), (a0, a1, a2), -1)
WRAP_RET(xmlChar *, xmlGetNsProp, (const xmlNode * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlEntity *, xmlGetParameterEntity, (xmlDoc * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlEntity *, xmlGetPredefinedEntity, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlGetProp, (const xmlNode * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlList *, xmlGetRefs, (xmlDoc * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(int, xmlGetUTF8Char, (const unsigned char * a0, int * a1), (a0, a1), -1)
WRAP_RET(int, xmlHasFeature, (xmlFeature a0), (a0), -1)
WRAP_RET(xmlAttr *, xmlHasNsProp, (const xmlNode * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlAttr *, xmlHasProp, (const xmlNode * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(int, xmlHashAdd, (xmlHashTable * a0, const xmlChar * a1, void * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlHashAdd2, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, void * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlHashAdd3, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, void * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlHashAddEntry, (xmlHashTable * a0, const xmlChar * a1, void * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlHashAddEntry2, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, void * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlHashAddEntry3, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, void * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(xmlHashTable *, xmlHashCopy, (xmlHashTable * a0, xmlHashCopier a1), (a0, a1), NULL)
WRAP_RET(xmlHashTable *, xmlHashCopySafe, (xmlHashTable * a0, xmlHashCopier a1, xmlHashDeallocator a2), (a0, a1, a2), NULL)
WRAP_RET(xmlHashTable *, xmlHashCreate, (int a0), (a0), NULL)
WRAP_RET(xmlHashTable *, xmlHashCreateDict, (int a0, xmlDict * a1), (a0, a1), NULL)
WRAP_VOID(xmlHashDefaultDeallocator, (void * a0, const xmlChar * a1), (a0, a1))
WRAP_VOID(xmlHashFree, (xmlHashTable * a0, xmlHashDeallocator a1), (a0, a1))
WRAP_RET(void *, xmlHashLookup, (xmlHashTable * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(void *, xmlHashLookup2, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(void *, xmlHashLookup3, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(void *, xmlHashQLookup, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(void *, xmlHashQLookup2, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, const xmlChar * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(void *, xmlHashQLookup3, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, const xmlChar * a4, const xmlChar * a5, const xmlChar * a6), (a0, a1, a2, a3, a4, a5, a6), NULL)
WRAP_RET(int, xmlHashRemoveEntry, (xmlHashTable * a0, const xmlChar * a1, xmlHashDeallocator a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlHashRemoveEntry2, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, xmlHashDeallocator a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlHashRemoveEntry3, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, xmlHashDeallocator a4), (a0, a1, a2, a3, a4), -1)
WRAP_VOID(xmlHashScan, (xmlHashTable * a0, xmlHashScanner a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlHashScan3, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, xmlHashScanner a4, void * a5), (a0, a1, a2, a3, a4, a5))
WRAP_VOID(xmlHashScanFull, (xmlHashTable * a0, xmlHashScannerFull a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlHashScanFull3, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, xmlHashScannerFull a4, void * a5), (a0, a1, a2, a3, a4, a5))
WRAP_RET(int, xmlHashSize, (xmlHashTable * a0), (a0), -1)
WRAP_RET(int, xmlHashUpdateEntry, (xmlHashTable * a0, const xmlChar * a1, void * a2, xmlHashDeallocator a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlHashUpdateEntry2, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, void * a3, xmlHashDeallocator a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlHashUpdateEntry3, (xmlHashTable * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, void * a4, xmlHashDeallocator a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlIOHTTPClose, (void * a0), (a0), -1)
WRAP_RET(int, xmlIOHTTPMatch, (const char * a0), (a0), -1)
WRAP_RET(void *, xmlIOHTTPOpen, (const char * a0), (a0), NULL)
WRAP_RET(void *, xmlIOHTTPOpenW, (const char * a0, int a1), (a0, a1), NULL)
WRAP_RET(int, xmlIOHTTPRead, (void * a0, char * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(xmlDtd *, xmlIOParseDTD, (xmlSAXHandler * a0, xmlParserInputBuffer * a1, xmlCharEncoding a2), (a0, a1, a2), NULL)
WRAP_VOID(xmlInitCharEncodingHandlers, (void), ())
WRAP_VOID(xmlInitGlobals, (void), ())
WRAP_RET(int, xmlInitMemory, (void), (), -1)
WRAP_VOID(xmlInitNodeInfoSeq, (xmlParserNodeInfoSeq * a0), (a0))
WRAP_VOID(xmlInitParser, (void), ())
WRAP_RET(int, xmlInitParserCtxt, (xmlParserCtxt * a0), (a0), -1)
WRAP_VOID(xmlInitThreads, (void), ())
WRAP_VOID(xmlInitializeCatalog, (void), ())
WRAP_RET(int, xmlInitializeDict, (void), (), -1)
WRAP_RET(xmlParserErrors, xmlInputSetEncodingHandler, (xmlParserInput * a0, xmlCharEncodingHandler * a1), (a0, a1), ((xmlParserErrors){0}))
WRAP_RET(int, xmlIsBaseChar, (unsigned int a0), (a0), -1)
WRAP_RET(int, xmlIsBlank, (unsigned int a0), (a0), -1)
WRAP_RET(int, xmlIsBlankNode, (const xmlNode * a0), (a0), -1)
WRAP_RET(int, xmlIsChar, (unsigned int a0), (a0), -1)
WRAP_RET(int, xmlIsCombining, (unsigned int a0), (a0), -1)
WRAP_RET(int, xmlIsDigit, (unsigned int a0), (a0), -1)
WRAP_RET(int, xmlIsExtender, (unsigned int a0), (a0), -1)
WRAP_RET(int, xmlIsID, (xmlDoc * a0, xmlNode * a1, xmlAttr * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlIsIdeographic, (unsigned int a0), (a0), -1)
WRAP_RET(int, xmlIsLetter, (int a0), (a0), -1)
WRAP_RET(int, xmlIsMixedElement, (xmlDoc * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlIsPubidChar, (unsigned int a0), (a0), -1)
WRAP_RET(int, xmlIsRef, (xmlDoc * a0, xmlNode * a1, xmlAttr * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlIsXHTML, (const xmlChar * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlIsolat1ToUTF8, (unsigned char * a0, int * a1, const unsigned char * a2, int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlKeepBlanksDefault, (int a0), (a0), -1)
WRAP_RET(xmlNode *, xmlLastElementChild, (xmlNode * a0), (a0), NULL)
WRAP_RET(int, xmlLineNumbersDefault, (int a0), (a0), -1)
WRAP_RET(void *, xmlLinkGetData, (xmlLink * a0), (a0), NULL)
WRAP_RET(int, xmlListAppend, (xmlList * a0, void * a1), (a0, a1), -1)
WRAP_VOID(xmlListClear, (xmlList * a0), (a0))
WRAP_RET(int, xmlListCopy, (xmlList * a0, xmlList * a1), (a0, a1), -1)
WRAP_RET(xmlList *, xmlListCreate, (xmlListDeallocator a0, xmlListDataCompare a1), (a0, a1), NULL)
WRAP_VOID(xmlListDelete, (xmlList * a0), (a0))
WRAP_RET(xmlList *, xmlListDup, (xmlList * a0), (a0), NULL)
WRAP_RET(int, xmlListEmpty, (xmlList * a0), (a0), -1)
WRAP_RET(xmlLink *, xmlListEnd, (xmlList * a0), (a0), NULL)
WRAP_RET(xmlLink *, xmlListFront, (xmlList * a0), (a0), NULL)
WRAP_RET(int, xmlListInsert, (xmlList * a0, void * a1), (a0, a1), -1)
WRAP_VOID(xmlListMerge, (xmlList * a0, xmlList * a1), (a0, a1))
WRAP_VOID(xmlListPopBack, (xmlList * a0), (a0))
WRAP_VOID(xmlListPopFront, (xmlList * a0), (a0))
WRAP_RET(int, xmlListPushBack, (xmlList * a0, void * a1), (a0, a1), -1)
WRAP_RET(int, xmlListPushFront, (xmlList * a0, void * a1), (a0, a1), -1)
WRAP_RET(int, xmlListRemoveAll, (xmlList * a0, void * a1), (a0, a1), -1)
WRAP_RET(int, xmlListRemoveFirst, (xmlList * a0, void * a1), (a0, a1), -1)
WRAP_RET(int, xmlListRemoveLast, (xmlList * a0, void * a1), (a0, a1), -1)
WRAP_VOID(xmlListReverse, (xmlList * a0), (a0))
WRAP_RET(void *, xmlListReverseSearch, (xmlList * a0, void * a1), (a0, a1), NULL)
WRAP_VOID(xmlListReverseWalk, (xmlList * a0, xmlListWalker a1, void * a2), (a0, a1, a2))
WRAP_RET(void *, xmlListSearch, (xmlList * a0, void * a1), (a0, a1), NULL)
WRAP_RET(int, xmlListSize, (xmlList * a0), (a0), -1)
WRAP_VOID(xmlListSort, (xmlList * a0), (a0))
WRAP_VOID(xmlListWalk, (xmlList * a0, xmlListWalker a1, void * a2), (a0, a1, a2))
WRAP_RET(xmlCatalog *, xmlLoadACatalog, (const char * a0), (a0), NULL)
WRAP_RET(int, xmlLoadCatalog, (const char * a0), (a0), -1)
WRAP_VOID(xmlLoadCatalogs, (const char * a0), (a0))
WRAP_RET(xmlParserInput *, xmlLoadExternalEntity, (const char * a0, const char * a1, xmlParserCtxt * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlCatalog *, xmlLoadSGMLSuperCatalog, (const char * a0), (a0), NULL)
WRAP_VOID(xmlLockLibrary, (void), ())
WRAP_RET(xmlParserErrors, xmlLookupCharEncodingHandler, (xmlCharEncoding a0, xmlCharEncodingHandler ** a1), (a0, a1), ((xmlParserErrors){0}))
WRAP_RET(void *, xmlMallocAtomicLoc, (size_t a0, const char * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(void *, xmlMallocLoc, (size_t a0, const char * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlMemBlocks, (void), (), -1)
WRAP_VOID(xmlMemDisplay, (FILE * a0), (a0))
WRAP_VOID(xmlMemDisplayLast, (FILE * a0, long int a1), (a0, a1))
WRAP_VOID(xmlMemFree, (void * a0), (a0))
WRAP_RET(int, xmlMemGet, (xmlFreeFunc * a0, xmlMallocFunc * a1, xmlReallocFunc * a2, xmlStrdupFunc * a3), (a0, a1, a2, a3), -1)
WRAP_RET(void *, xmlMemMalloc, (size_t a0), (a0), NULL)
WRAP_RET(void *, xmlMemRealloc, (void * a0, size_t a1), (a0, a1), NULL)
WRAP_RET(int, xmlMemSetup, (xmlFreeFunc a0, xmlMallocFunc a1, xmlReallocFunc a2, xmlStrdupFunc a3), (a0, a1, a2, a3), -1)
WRAP_VOID(xmlMemShow, (FILE * a0, int a1), (a0, a1))
WRAP_RET(size_t, xmlMemSize, (void * a0), (a0), ((size_t){0}))
WRAP_RET(char *, xmlMemStrdupLoc, (const char * a0, const char * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlMemUsed, (void), (), -1)
WRAP_VOID(xmlMemoryDump, (void), ())
WRAP_RET(char *, xmlMemoryStrdup, (const char * a0), (a0), NULL)
WRAP_RET(int, xmlModuleClose, (xmlModule * a0), (a0), -1)
WRAP_RET(int, xmlModuleFree, (xmlModule * a0), (a0), -1)
WRAP_RET(xmlModule *, xmlModuleOpen, (const char * a0, int a1), (a0, a1), NULL)
WRAP_RET(int, xmlModuleSymbol, (xmlModule * a0, const char * a1, void ** a2), (a0, a1, a2), -1)
WRAP_VOID(xmlMutexLock, (xmlMutex * a0), (a0))
WRAP_VOID(xmlMutexUnlock, (xmlMutex * a0), (a0))
WRAP_RET(const char *, xmlNanoHTTPAuthHeader, (void * a0), (a0), NULL)
WRAP_VOID(xmlNanoHTTPCleanup, (void), ())
WRAP_VOID(xmlNanoHTTPClose, (void * a0), (a0))
WRAP_RET(int, xmlNanoHTTPContentLength, (void * a0), (a0), -1)
WRAP_RET(const char *, xmlNanoHTTPEncoding, (void * a0), (a0), NULL)
WRAP_RET(int, xmlNanoHTTPFetch, (const char * a0, const char * a1, char ** a2), (a0, a1, a2), -1)
WRAP_VOID(xmlNanoHTTPInit, (void), ())
WRAP_RET(void *, xmlNanoHTTPMethod, (const char * a0, const char * a1, const char * a2, char ** a3, const char * a4, int a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(void *, xmlNanoHTTPMethodRedir, (const char * a0, const char * a1, const char * a2, char ** a3, char ** a4, const char * a5, int a6), (a0, a1, a2, a3, a4, a5, a6), NULL)
WRAP_RET(const char *, xmlNanoHTTPMimeType, (void * a0), (a0), NULL)
WRAP_RET(void *, xmlNanoHTTPOpen, (const char * a0, char ** a1), (a0, a1), NULL)
WRAP_RET(void *, xmlNanoHTTPOpenRedir, (const char * a0, char ** a1, char ** a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlNanoHTTPRead, (void * a0, void * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(const char *, xmlNanoHTTPRedir, (void * a0), (a0), NULL)
WRAP_RET(int, xmlNanoHTTPReturnCode, (void * a0), (a0), -1)
WRAP_RET(int, xmlNanoHTTPSave, (void * a0, const char * a1), (a0, a1), -1)
WRAP_VOID(xmlNanoHTTPScanProxy, (const char * a0), (a0))
WRAP_RET(xmlAutomata *, xmlNewAutomata, (void), (), NULL)
WRAP_RET(xmlNode *, xmlNewCDataBlock, (xmlDoc * a0, const xmlChar * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlCatalog *, xmlNewCatalog, (int a0), (a0), NULL)
WRAP_RET(xmlCharEncodingHandler *, xmlNewCharEncodingHandler, (const char * a0, xmlCharEncodingInputFunc a1, xmlCharEncodingOutputFunc a2), (a0, a1, a2), NULL)
WRAP_RET(xmlNode *, xmlNewCharRef, (xmlDoc * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlNewChild, (xmlNode * a0, xmlNs * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlNode *, xmlNewComment, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlDoc *, xmlNewDoc, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlNode *, xmlNewDocComment, (xmlDoc * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlElementContent *, xmlNewDocElementContent, (xmlDoc * a0, const xmlChar * a1, xmlElementContentType a2), (a0, a1, a2), NULL)
WRAP_RET(xmlNode *, xmlNewDocFragment, (xmlDoc * a0), (a0), NULL)
WRAP_RET(xmlNode *, xmlNewDocNode, (xmlDoc * a0, xmlNs * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlNode *, xmlNewDocNodeEatName, (xmlDoc * a0, xmlNs * a1, xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlNode *, xmlNewDocPI, (xmlDoc * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlAttr *, xmlNewDocProp, (xmlDoc * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlNode *, xmlNewDocRawNode, (xmlDoc * a0, xmlNs * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlNode *, xmlNewDocText, (const xmlDoc * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlNewDocTextLen, (xmlDoc * a0, const xmlChar * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlDtd *, xmlNewDtd, (xmlDoc * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlElementContent *, xmlNewElementContent, (const xmlChar * a0, xmlElementContentType a1), (a0, a1), NULL)
WRAP_RET(xmlEntity *, xmlNewEntity, (xmlDoc * a0, const xmlChar * a1, int a2, const xmlChar * a3, const xmlChar * a4, const xmlChar * a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(xmlParserInput *, xmlNewEntityInputStream, (xmlParserCtxt * a0, xmlEntity * a1), (a0, a1), NULL)
WRAP_RET(xmlParserInput *, xmlNewIOInputStream, (xmlParserCtxt * a0, xmlParserInputBuffer * a1, xmlCharEncoding a2), (a0, a1, a2), NULL)
WRAP_RET(xmlParserInput *, xmlNewInputFromFd, (const char * a0, int a1, xmlParserInputFlags a2), (a0, a1, a2), NULL)
WRAP_RET(xmlParserInput *, xmlNewInputFromFile, (xmlParserCtxt * a0, const char * a1), (a0, a1), NULL)
WRAP_RET(xmlParserInput *, xmlNewInputFromIO, (const char * a0, xmlInputReadCallback a1, xmlInputCloseCallback a2, void * a3, xmlParserInputFlags a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(xmlParserInput *, xmlNewInputFromMemory, (const char * a0, const void * a1, size_t a2, xmlParserInputFlags a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlParserInput *, xmlNewInputFromString, (const char * a0, const char * a1, xmlParserInputFlags a2), (a0, a1, a2), NULL)
WRAP_RET(xmlParserErrors, xmlNewInputFromUrl, (const char * a0, xmlParserInputFlags a1, xmlParserInput ** a2), (a0, a1, a2), ((xmlParserErrors){0}))
WRAP_RET(xmlParserInput *, xmlNewInputStream, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(xmlMutex *, xmlNewMutex, (void), (), NULL)
WRAP_RET(xmlNode *, xmlNewNode, (xmlNs * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlNewNodeEatName, (xmlNs * a0, xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlNs *, xmlNewNs, (xmlNode * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlAttr *, xmlNewNsProp, (xmlNode * a0, xmlNs * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlAttr *, xmlNewNsPropEatName, (xmlNode * a0, xmlNs * a1, xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlNode *, xmlNewPI, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlParserCtxt *, xmlNewParserCtxt, (void), (), NULL)
WRAP_RET(xmlAttr *, xmlNewProp, (xmlNode * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlRMutex *, xmlNewRMutex, (void), (), NULL)
WRAP_RET(xmlNode *, xmlNewReference, (const xmlDoc * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlParserCtxt *, xmlNewSAXParserCtxt, (const xmlSAXHandler * a0, void * a1), (a0, a1), NULL)
WRAP_RET(xmlParserInput *, xmlNewStringInputStream, (xmlParserCtxt * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlNewText, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlNode *, xmlNewTextChild, (xmlNode * a0, xmlNs * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlNode *, xmlNewTextLen, (const xmlChar * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlTextReader *, xmlNewTextReader, (xmlParserInputBuffer * a0, const char * a1), (a0, a1), NULL)
WRAP_RET(xmlTextReader *, xmlNewTextReaderFilename, (const char * a0), (a0), NULL)
WRAP_RET(xmlTextWriter *, xmlNewTextWriter, (xmlOutputBuffer * a0), (a0), NULL)
WRAP_RET(xmlTextWriter *, xmlNewTextWriterDoc, (xmlDoc ** a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlTextWriter *, xmlNewTextWriterFilename, (const char * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlTextWriter *, xmlNewTextWriterMemory, (xmlBuffer * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlTextWriter *, xmlNewTextWriterPushParser, (xmlParserCtxt * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlTextWriter *, xmlNewTextWriterTree, (xmlDoc * a0, xmlNode * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlValidCtxt *, xmlNewValidCtxt, (void), (), NULL)
WRAP_VOID(xmlNextChar, (xmlParserCtxt * a0), (a0))
WRAP_RET(xmlNode *, xmlNextElementSibling, (xmlNode * a0), (a0), NULL)
WRAP_RET(xmlParserInput *, xmlNoNetExternalEntityLoader, (const char * a0, const char * a1, xmlParserCtxt * a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlNodeAddContent, (xmlNode * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlNodeAddContentLen, (xmlNode * a0, const xmlChar * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlNodeBufGetContent, (xmlBuffer * a0, const xmlNode * a1), (a0, a1), -1)
WRAP_RET(int, xmlNodeDump, (xmlBuffer * a0, xmlDoc * a1, xmlNode * a2, int a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_VOID(xmlNodeDumpOutput, (xmlOutputBuffer * a0, xmlDoc * a1, xmlNode * a2, int a3, int a4, const char * a5), (a0, a1, a2, a3, a4, a5))
WRAP_RET(int, xmlNodeGetAttrValue, (const xmlNode * a0, const xmlChar * a1, const xmlChar * a2, xmlChar ** a3), (a0, a1, a2, a3), -1)
WRAP_RET(xmlChar *, xmlNodeGetBase, (const xmlDoc * a0, const xmlNode * a1), (a0, a1), NULL)
WRAP_RET(int, xmlNodeGetBaseSafe, (const xmlDoc * a0, const xmlNode * a1, xmlChar ** a2), (a0, a1, a2), -1)
WRAP_RET(xmlChar *, xmlNodeGetContent, (const xmlNode * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlNodeGetLang, (const xmlNode * a0), (a0), NULL)
WRAP_RET(int, xmlNodeGetSpacePreserve, (const xmlNode * a0), (a0), -1)
WRAP_RET(int, xmlNodeIsText, (const xmlNode * a0), (a0), -1)
WRAP_RET(xmlChar *, xmlNodeListGetRawString, (const xmlDoc * a0, const xmlNode * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlChar *, xmlNodeListGetString, (xmlDoc * a0, const xmlNode * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlNodeSetBase, (xmlNode * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlNodeSetContent, (xmlNode * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlNodeSetContentLen, (xmlNode * a0, const xmlChar * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlNodeSetLang, (xmlNode * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_VOID(xmlNodeSetName, (xmlNode * a0, const xmlChar * a1), (a0, a1))
WRAP_RET(int, xmlNodeSetSpacePreserve, (xmlNode * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlNormalizeURIPath, (char * a0), (a0), -1)
WRAP_RET(xmlChar *, xmlNormalizeWindowsPath, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlParserErrors, xmlOpenCharEncodingHandler, (const char * a0, int a1, xmlCharEncodingHandler ** a2), (a0, a1, a2), ((xmlParserErrors){0}))
WRAP_RET(int, xmlOutputBufferClose, (xmlOutputBuffer * a0), (a0), -1)
WRAP_RET(xmlOutputBuffer *, xmlOutputBufferCreateBuffer, (xmlBuffer * a0, xmlCharEncodingHandler * a1), (a0, a1), NULL)
WRAP_RET(xmlOutputBuffer *, xmlOutputBufferCreateFd, (int a0, xmlCharEncodingHandler * a1), (a0, a1), NULL)
WRAP_RET(xmlOutputBuffer *, xmlOutputBufferCreateFile, (FILE * a0, xmlCharEncodingHandler * a1), (a0, a1), NULL)
WRAP_RET(xmlOutputBuffer *, xmlOutputBufferCreateFilename, (const char * a0, xmlCharEncodingHandler * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlOutputBufferCreateFilenameFunc, xmlOutputBufferCreateFilenameDefault, (xmlOutputBufferCreateFilenameFunc a0), (a0), ((xmlOutputBufferCreateFilenameFunc){0}))
WRAP_RET(xmlOutputBuffer *, xmlOutputBufferCreateIO, (xmlOutputWriteCallback a0, xmlOutputCloseCallback a1, void * a2, xmlCharEncodingHandler * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(int, xmlOutputBufferFlush, (xmlOutputBuffer * a0), (a0), -1)
WRAP_RET(const xmlChar *, xmlOutputBufferGetContent, (xmlOutputBuffer * a0), (a0), NULL)
WRAP_RET(size_t, xmlOutputBufferGetSize, (xmlOutputBuffer * a0), (a0), ((size_t){0}))
WRAP_RET(int, xmlOutputBufferWrite, (xmlOutputBuffer * a0, int a1, const char * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlOutputBufferWriteEscape, (xmlOutputBuffer * a0, const xmlChar * a1, xmlCharEncodingOutputFunc a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlOutputBufferWriteString, (xmlOutputBuffer * a0, const char * a1), (a0, a1), -1)
WRAP_RET(xmlChar *, xmlParseAttValue, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlParseAttribute, (xmlParserCtxt * a0, xmlChar ** a1), (a0, a1), NULL)
WRAP_VOID(xmlParseAttributeListDecl, (xmlParserCtxt * a0), (a0))
WRAP_RET(int, xmlParseAttributeType, (xmlParserCtxt * a0, xmlEnumeration ** a1), (a0, a1), -1)
WRAP_RET(int, xmlParseBalancedChunkMemory, (xmlDoc * a0, xmlSAXHandler * a1, void * a2, int a3, const xmlChar * a4, xmlNode ** a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlParseBalancedChunkMemoryRecover, (xmlDoc * a0, xmlSAXHandler * a1, void * a2, int a3, const xmlChar * a4, xmlNode ** a5, int a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_VOID(xmlParseCDSect, (xmlParserCtxt * a0), (a0))
WRAP_RET(xmlDoc *, xmlParseCatalogFile, (const char * a0), (a0), NULL)
WRAP_VOID(xmlParseCharData, (xmlParserCtxt * a0, int a1), (a0, a1))
WRAP_RET(xmlCharEncoding, xmlParseCharEncoding, (const char * a0), (a0), ((xmlCharEncoding){0}))
WRAP_RET(int, xmlParseCharRef, (xmlParserCtxt * a0), (a0), -1)
WRAP_RET(int, xmlParseChunk, (xmlParserCtxt * a0, const char * a1, int a2, int a3), (a0, a1, a2, a3), -1)
WRAP_VOID(xmlParseComment, (xmlParserCtxt * a0), (a0))
WRAP_VOID(xmlParseContent, (xmlParserCtxt * a0), (a0))
WRAP_RET(int, xmlParseCtxtExternalEntity, (xmlParserCtxt * a0, const xmlChar * a1, const xmlChar * a2, xmlNode ** a3), (a0, a1, a2, a3), -1)
WRAP_RET(xmlDtd *, xmlParseDTD, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(int, xmlParseDefaultDecl, (xmlParserCtxt * a0, xmlChar ** a1), (a0, a1), -1)
WRAP_RET(xmlDoc *, xmlParseDoc, (const xmlChar * a0), (a0), NULL)
WRAP_VOID(xmlParseDocTypeDecl, (xmlParserCtxt * a0), (a0))
WRAP_RET(int, xmlParseDocument, (xmlParserCtxt * a0), (a0), -1)
WRAP_VOID(xmlParseElement, (xmlParserCtxt * a0), (a0))
WRAP_RET(xmlElementContent *, xmlParseElementChildrenContentDecl, (xmlParserCtxt * a0, int a1), (a0, a1), NULL)
WRAP_RET(int, xmlParseElementContentDecl, (xmlParserCtxt * a0, const xmlChar * a1, xmlElementContent ** a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlParseElementDecl, (xmlParserCtxt * a0), (a0), -1)
WRAP_RET(xmlElementContent *, xmlParseElementMixedContentDecl, (xmlParserCtxt * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlChar *, xmlParseEncName, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlParseEncodingDecl, (xmlParserCtxt * a0), (a0), NULL)
WRAP_VOID(xmlParseEndTag, (xmlParserCtxt * a0), (a0))
WRAP_RET(xmlDoc *, xmlParseEntity, (const char * a0), (a0), NULL)
WRAP_VOID(xmlParseEntityDecl, (xmlParserCtxt * a0), (a0))
WRAP_RET(xmlEntity *, xmlParseEntityRef, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlParseEntityValue, (xmlParserCtxt * a0, xmlChar ** a1), (a0, a1), NULL)
WRAP_RET(int, xmlParseEnumeratedType, (xmlParserCtxt * a0, xmlEnumeration ** a1), (a0, a1), -1)
WRAP_RET(xmlEnumeration *, xmlParseEnumerationType, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(int, xmlParseExtParsedEnt, (xmlParserCtxt * a0), (a0), -1)
WRAP_RET(int, xmlParseExternalEntity, (xmlDoc * a0, xmlSAXHandler * a1, void * a2, int a3, const xmlChar * a4, const xmlChar * a5, xmlNode ** a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(xmlChar *, xmlParseExternalID, (xmlParserCtxt * a0, xmlChar ** a1, int a2), (a0, a1, a2), NULL)
WRAP_VOID(xmlParseExternalSubset, (xmlParserCtxt * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2))
WRAP_RET(xmlDoc *, xmlParseFile, (const char * a0), (a0), NULL)
WRAP_RET(xmlParserErrors, xmlParseInNodeContext, (xmlNode * a0, const char * a1, int a2, int a3, xmlNode ** a4), (a0, a1, a2, a3, a4), ((xmlParserErrors){0}))
WRAP_VOID(xmlParseMarkupDecl, (xmlParserCtxt * a0), (a0))
WRAP_RET(xmlDoc *, xmlParseMemory, (const char * a0, int a1), (a0, a1), NULL)
WRAP_VOID(xmlParseMisc, (xmlParserCtxt * a0), (a0))
WRAP_RET(const xmlChar *, xmlParseName, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlParseNmtoken, (xmlParserCtxt * a0), (a0), NULL)
WRAP_VOID(xmlParseNotationDecl, (xmlParserCtxt * a0), (a0))
WRAP_RET(xmlEnumeration *, xmlParseNotationType, (xmlParserCtxt * a0), (a0), NULL)
WRAP_VOID(xmlParsePEReference, (xmlParserCtxt * a0), (a0))
WRAP_VOID(xmlParsePI, (xmlParserCtxt * a0), (a0))
WRAP_RET(const xmlChar *, xmlParsePITarget, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlParsePubidLiteral, (xmlParserCtxt * a0), (a0), NULL)
WRAP_VOID(xmlParseReference, (xmlParserCtxt * a0), (a0))
WRAP_RET(int, xmlParseSDDecl, (xmlParserCtxt * a0), (a0), -1)
WRAP_RET(const xmlChar *, xmlParseStartTag, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlParseSystemLiteral, (xmlParserCtxt * a0), (a0), NULL)
WRAP_VOID(xmlParseTextDecl, (xmlParserCtxt * a0), (a0))
WRAP_RET(xmlURI *, xmlParseURI, (const char * a0), (a0), NULL)
WRAP_RET(xmlURI *, xmlParseURIRaw, (const char * a0, int a1), (a0, a1), NULL)
WRAP_RET(int, xmlParseURIReference, (xmlURI * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, xmlParseURISafe, (const char * a0, xmlURI ** a1), (a0, a1), -1)
WRAP_RET(xmlChar *, xmlParseVersionInfo, (xmlParserCtxt * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlParseVersionNum, (xmlParserCtxt * a0), (a0), NULL)
WRAP_VOID(xmlParseXMLDecl, (xmlParserCtxt * a0), (a0))
WRAP_VOID(xmlParserAddNodeInfo, (xmlParserCtxt * a0, xmlParserNodeInfo * a1), (a0, a1))
VFWD_VOID(xmlParserError, (void * a0, const char * a1, ...), (void *, const char *), (a0, "%s", b), a1)
WRAP_RET(const xmlParserNodeInfo *, xmlParserFindNodeInfo, (xmlParserCtxt * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(long unsigned int, xmlParserFindNodeInfoIndex, (xmlParserNodeInfoSeq * a0, xmlNode * a1), (a0, a1), ((long unsigned int){0}))
WRAP_RET(char *, xmlParserGetDirectory, (const char * a0), (a0), NULL)
WRAP_VOID(xmlParserHandlePEReference, (xmlParserCtxt * a0), (a0))
WRAP_RET(xmlParserInputBuffer *, xmlParserInputBufferCreateFd, (int a0, xmlCharEncoding a1), (a0, a1), NULL)
WRAP_RET(xmlParserInputBuffer *, xmlParserInputBufferCreateFile, (FILE * a0, xmlCharEncoding a1), (a0, a1), NULL)
WRAP_RET(xmlParserInputBuffer *, xmlParserInputBufferCreateFilename, (const char * a0, xmlCharEncoding a1), (a0, a1), NULL)
WRAP_RET(xmlParserInputBufferCreateFilenameFunc, xmlParserInputBufferCreateFilenameDefault, (xmlParserInputBufferCreateFilenameFunc a0), (a0), ((xmlParserInputBufferCreateFilenameFunc){0}))
WRAP_RET(xmlParserInputBuffer *, xmlParserInputBufferCreateIO, (xmlInputReadCallback a0, xmlInputCloseCallback a1, void * a2, xmlCharEncoding a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlParserInputBuffer *, xmlParserInputBufferCreateMem, (const char * a0, int a1, xmlCharEncoding a2), (a0, a1, a2), NULL)
WRAP_RET(xmlParserInputBuffer *, xmlParserInputBufferCreateStatic, (const char * a0, int a1, xmlCharEncoding a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlParserInputBufferGrow, (xmlParserInputBuffer * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlParserInputBufferPush, (xmlParserInputBuffer * a0, int a1, const char * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlParserInputBufferRead, (xmlParserInputBuffer * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlParserInputGrow, (xmlParserInput * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlParserInputRead, (xmlParserInput * a0, int a1), (a0, a1), -1)
WRAP_VOID(xmlParserInputShrink, (xmlParserInput * a0), (a0))
WRAP_VOID(xmlParserPrintFileContext, (struct _xmlParserInput * a0), (a0))
WRAP_VOID(xmlParserPrintFileInfo, (struct _xmlParserInput * a0), (a0))
VFWD_VOID(xmlParserValidityError, (void * a0, const char * a1, ...), (void *, const char *), (a0, "%s", b), a1)
VFWD_VOID(xmlParserValidityWarning, (void * a0, const char * a1, ...), (void *, const char *), (a0, "%s", b), a1)
VFWD_VOID(xmlParserWarning, (void * a0, const char * a1, ...), (void *, const char *), (a0, "%s", b), a1)
WRAP_RET(xmlChar *, xmlPathToURI, (const xmlChar * a0), (a0), NULL)
WRAP_RET(int, xmlPatternCompileSafe, (const xmlChar * a0, xmlDict * a1, int a2, const xmlChar ** a3, xmlPattern ** a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlPatternFromRoot, (xmlPattern * a0), (a0), -1)
WRAP_RET(xmlStreamCtxt *, xmlPatternGetStreamCtxt, (xmlPattern * a0), (a0), NULL)
WRAP_RET(int, xmlPatternMatch, (xmlPattern * a0, xmlNode * a1), (a0, a1), -1)
WRAP_RET(int, xmlPatternMaxDepth, (xmlPattern * a0), (a0), -1)
WRAP_RET(int, xmlPatternMinDepth, (xmlPattern * a0), (a0), -1)
WRAP_RET(int, xmlPatternStreamable, (xmlPattern * a0), (a0), -1)
WRAP_RET(xmlPattern *, xmlPatterncompile, (const xmlChar * a0, xmlDict * a1, int a2, const xmlChar ** a3), (a0, a1, a2, a3), NULL)
WRAP_RET(int, xmlPedanticParserDefault, (int a0), (a0), -1)
WRAP_RET(xmlChar, xmlPopInput, (xmlParserCtxt * a0), (a0), ((xmlChar){0}))
WRAP_RET(int, xmlPopInputCallbacks, (void), (), -1)
WRAP_RET(int, xmlPopOutputCallbacks, (void), (), -1)
WRAP_RET(xmlNode *, xmlPreviousElementSibling, (xmlNode * a0), (a0), NULL)
WRAP_VOID(xmlPrintURI, (FILE * a0, xmlURI * a1), (a0, a1))
WRAP_RET(int, xmlPushInput, (xmlParserCtxt * a0, xmlParserInput * a1), (a0, a1), -1)
WRAP_VOID(xmlRMutexLock, (xmlRMutex * a0), (a0))
WRAP_VOID(xmlRMutexUnlock, (xmlRMutex * a0), (a0))
WRAP_RET(xmlDoc *, xmlReadDoc, (const xmlChar * a0, const char * a1, const char * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlDoc *, xmlReadFd, (int a0, const char * a1, const char * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlDoc *, xmlReadFile, (const char * a0, const char * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlDoc *, xmlReadIO, (xmlInputReadCallback a0, xmlInputCloseCallback a1, void * a2, const char * a3, const char * a4, int a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(xmlDoc *, xmlReadMemory, (const char * a0, int a1, const char * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(xmlTextReader *, xmlReaderForDoc, (const xmlChar * a0, const char * a1, const char * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlTextReader *, xmlReaderForFd, (int a0, const char * a1, const char * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlTextReader *, xmlReaderForFile, (const char * a0, const char * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlTextReader *, xmlReaderForIO, (xmlInputReadCallback a0, xmlInputCloseCallback a1, void * a2, const char * a3, const char * a4, int a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(xmlTextReader *, xmlReaderForMemory, (const char * a0, int a1, const char * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(int, xmlReaderNewDoc, (xmlTextReader * a0, const xmlChar * a1, const char * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlReaderNewFd, (xmlTextReader * a0, int a1, const char * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlReaderNewFile, (xmlTextReader * a0, const char * a1, const char * a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlReaderNewIO, (xmlTextReader * a0, xmlInputReadCallback a1, xmlInputCloseCallback a2, void * a3, const char * a4, const char * a5, int a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, xmlReaderNewMemory, (xmlTextReader * a0, const char * a1, int a2, const char * a3, const char * a4, int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlReaderNewWalker, (xmlTextReader * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_RET(xmlTextReader *, xmlReaderWalker, (xmlDoc * a0), (a0), NULL)
WRAP_RET(void *, xmlReallocLoc, (void * a0, size_t a1, const char * a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(int, xmlReconciliateNs, (xmlDoc * a0, xmlNode * a1), (a0, a1), -1)
WRAP_RET(xmlDoc *, xmlRecoverDoc, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlDoc *, xmlRecoverFile, (const char * a0), (a0), NULL)
WRAP_RET(xmlDoc *, xmlRecoverMemory, (const char * a0, int a1), (a0, a1), NULL)
WRAP_RET(int, xmlRegExecErrInfo, (xmlRegExecCtxt * a0, const xmlChar ** a1, int * a2, int * a3, xmlChar ** a4, int * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlRegExecNextValues, (xmlRegExecCtxt * a0, int * a1, int * a2, xmlChar ** a3, int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlRegExecPushString, (xmlRegExecCtxt * a0, const xmlChar * a1, void * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlRegExecPushString2, (xmlRegExecCtxt * a0, const xmlChar * a1, const xmlChar * a2, void * a3), (a0, a1, a2, a3), -1)
WRAP_VOID(xmlRegFreeExecCtxt, (xmlRegExecCtxt * a0), (a0))
WRAP_VOID(xmlRegFreeRegexp, (xmlRegexp * a0), (a0))
WRAP_RET(xmlRegExecCtxt *, xmlRegNewExecCtxt, (xmlRegexp * a0, xmlRegExecCallbacks a1, void * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlRegexp *, xmlRegexpCompile, (const xmlChar * a0), (a0), NULL)
WRAP_RET(int, xmlRegexpExec, (xmlRegexp * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlRegexpIsDeterminist, (xmlRegexp * a0), (a0), -1)
WRAP_VOID(xmlRegexpPrint, (FILE * a0, xmlRegexp * a1), (a0, a1))
WRAP_VOID(xmlRegisterCharEncodingHandler, (xmlCharEncodingHandler * a0), (a0))
WRAP_VOID(xmlRegisterDefaultInputCallbacks, (void), ())
WRAP_VOID(xmlRegisterDefaultOutputCallbacks, (void), ())
WRAP_VOID(xmlRegisterHTTPPostCallbacks, (void), ())
WRAP_RET(int, xmlRegisterInputCallbacks, (xmlInputMatchCallback a0, xmlInputOpenCallback a1, xmlInputReadCallback a2, xmlInputCloseCallback a3), (a0, a1, a2, a3), -1)
WRAP_RET(xmlRegisterNodeFunc, xmlRegisterNodeDefault, (xmlRegisterNodeFunc a0), (a0), ((xmlRegisterNodeFunc){0}))
WRAP_RET(int, xmlRegisterOutputCallbacks, (xmlOutputMatchCallback a0, xmlOutputOpenCallback a1, xmlOutputWriteCallback a2, xmlOutputCloseCallback a3), (a0, a1, a2, a3), -1)
WRAP_VOID(xmlRelaxNGCleanupTypes, (void), ())
WRAP_VOID(xmlRelaxNGDump, (FILE * a0, xmlRelaxNG * a1), (a0, a1))
WRAP_VOID(xmlRelaxNGDumpTree, (FILE * a0, xmlRelaxNG * a1), (a0, a1))
WRAP_VOID(xmlRelaxNGFree, (xmlRelaxNG * a0), (a0))
WRAP_VOID(xmlRelaxNGFreeParserCtxt, (xmlRelaxNGParserCtxt * a0), (a0))
WRAP_VOID(xmlRelaxNGFreeValidCtxt, (xmlRelaxNGValidCtxt * a0), (a0))
WRAP_RET(int, xmlRelaxNGGetParserErrors, (xmlRelaxNGParserCtxt * a0, xmlRelaxNGValidityErrorFunc * a1, xmlRelaxNGValidityWarningFunc * a2, void ** a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlRelaxNGGetValidErrors, (xmlRelaxNGValidCtxt * a0, xmlRelaxNGValidityErrorFunc * a1, xmlRelaxNGValidityWarningFunc * a2, void ** a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlRelaxNGInitTypes, (void), (), -1)
WRAP_RET(xmlRelaxNGParserCtxt *, xmlRelaxNGNewDocParserCtxt, (xmlDoc * a0), (a0), NULL)
WRAP_RET(xmlRelaxNGParserCtxt *, xmlRelaxNGNewMemParserCtxt, (const char * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlRelaxNGParserCtxt *, xmlRelaxNGNewParserCtxt, (const char * a0), (a0), NULL)
WRAP_RET(xmlRelaxNGValidCtxt *, xmlRelaxNGNewValidCtxt, (xmlRelaxNG * a0), (a0), NULL)
WRAP_RET(xmlRelaxNG *, xmlRelaxNGParse, (xmlRelaxNGParserCtxt * a0), (a0), NULL)
WRAP_VOID(xmlRelaxNGSetParserErrors, (xmlRelaxNGParserCtxt * a0, xmlRelaxNGValidityErrorFunc a1, xmlRelaxNGValidityWarningFunc a2, void * a3), (a0, a1, a2, a3))
WRAP_VOID(xmlRelaxNGSetParserStructuredErrors, (xmlRelaxNGParserCtxt * a0, xmlStructuredErrorFunc a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlRelaxNGSetResourceLoader, (xmlRelaxNGParserCtxt * a0, xmlResourceLoader a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlRelaxNGSetValidErrors, (xmlRelaxNGValidCtxt * a0, xmlRelaxNGValidityErrorFunc a1, xmlRelaxNGValidityWarningFunc a2, void * a3), (a0, a1, a2, a3))
WRAP_VOID(xmlRelaxNGSetValidStructuredErrors, (xmlRelaxNGValidCtxt * a0, xmlStructuredErrorFunc a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlRelaxNGValidCtxtClearErrors, (xmlRelaxNGValidCtxt * a0), (a0))
WRAP_RET(int, xmlRelaxNGValidateDoc, (xmlRelaxNGValidCtxt * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_RET(int, xmlRelaxNGValidateFullElement, (xmlRelaxNGValidCtxt * a0, xmlDoc * a1, xmlNode * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlRelaxNGValidatePopElement, (xmlRelaxNGValidCtxt * a0, xmlDoc * a1, xmlNode * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlRelaxNGValidatePushCData, (xmlRelaxNGValidCtxt * a0, const xmlChar * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlRelaxNGValidatePushElement, (xmlRelaxNGValidCtxt * a0, xmlDoc * a1, xmlNode * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlRelaxParserSetFlag, (xmlRelaxNGParserCtxt * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlRelaxParserSetIncLImit, (xmlRelaxNGParserCtxt * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlRemoveID, (xmlDoc * a0, xmlAttr * a1), (a0, a1), -1)
WRAP_RET(int, xmlRemoveProp, (xmlAttr * a0), (a0), -1)
WRAP_RET(int, xmlRemoveRef, (xmlDoc * a0, xmlAttr * a1), (a0, a1), -1)
WRAP_RET(xmlNode *, xmlReplaceNode, (xmlNode * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_VOID(xmlResetError, (xmlError * a0), (a0))
WRAP_VOID(xmlResetLastError, (void), ())
WRAP_VOID(xmlSAX2AttributeDecl, (void * a0, const xmlChar * a1, const xmlChar * a2, int a3, int a4, const xmlChar * a5, xmlEnumeration * a6), (a0, a1, a2, a3, a4, a5, a6))
WRAP_VOID(xmlSAX2CDataBlock, (void * a0, const xmlChar * a1, int a2), (a0, a1, a2))
WRAP_VOID(xmlSAX2Characters, (void * a0, const xmlChar * a1, int a2), (a0, a1, a2))
WRAP_VOID(xmlSAX2Comment, (void * a0, const xmlChar * a1), (a0, a1))
WRAP_VOID(xmlSAX2ElementDecl, (void * a0, const xmlChar * a1, int a2, xmlElementContent * a3), (a0, a1, a2, a3))
WRAP_VOID(xmlSAX2EndDocument, (void * a0), (a0))
WRAP_VOID(xmlSAX2EndElement, (void * a0, const xmlChar * a1), (a0, a1))
WRAP_VOID(xmlSAX2EndElementNs, (void * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3))
WRAP_VOID(xmlSAX2EntityDecl, (void * a0, const xmlChar * a1, int a2, const xmlChar * a3, const xmlChar * a4, xmlChar * a5), (a0, a1, a2, a3, a4, a5))
WRAP_VOID(xmlSAX2ExternalSubset, (void * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3))
WRAP_RET(int, xmlSAX2GetColumnNumber, (void * a0), (a0), -1)
WRAP_RET(xmlEntity *, xmlSAX2GetEntity, (void * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(int, xmlSAX2GetLineNumber, (void * a0), (a0), -1)
WRAP_RET(xmlEntity *, xmlSAX2GetParameterEntity, (void * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(const xmlChar *, xmlSAX2GetPublicId, (void * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlSAX2GetSystemId, (void * a0), (a0), NULL)
WRAP_RET(int, xmlSAX2HasExternalSubset, (void * a0), (a0), -1)
WRAP_RET(int, xmlSAX2HasInternalSubset, (void * a0), (a0), -1)
WRAP_VOID(xmlSAX2IgnorableWhitespace, (void * a0, const xmlChar * a1, int a2), (a0, a1, a2))
WRAP_VOID(xmlSAX2InitDefaultSAXHandler, (xmlSAXHandler * a0, int a1), (a0, a1))
WRAP_VOID(xmlSAX2InitHtmlDefaultSAXHandler, (xmlSAXHandler * a0), (a0))
WRAP_VOID(xmlSAX2InternalSubset, (void * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3))
WRAP_RET(int, xmlSAX2IsStandalone, (void * a0), (a0), -1)
WRAP_VOID(xmlSAX2NotationDecl, (void * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3))
WRAP_VOID(xmlSAX2ProcessingInstruction, (void * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2))
WRAP_VOID(xmlSAX2Reference, (void * a0, const xmlChar * a1), (a0, a1))
WRAP_RET(xmlParserInput *, xmlSAX2ResolveEntity, (void * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_VOID(xmlSAX2SetDocumentLocator, (void * a0, xmlSAXLocator * a1), (a0, a1))
WRAP_VOID(xmlSAX2StartDocument, (void * a0), (a0))
WRAP_VOID(xmlSAX2StartElement, (void * a0, const xmlChar * a1, const xmlChar ** a2), (a0, a1, a2))
WRAP_VOID(xmlSAX2StartElementNs, (void * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, int a4, const xmlChar ** a5, int a6, int a7, const xmlChar ** a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8))
WRAP_VOID(xmlSAX2UnparsedEntityDecl, (void * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, const xmlChar * a4), (a0, a1, a2, a3, a4))
WRAP_RET(int, xmlSAXDefaultVersion, (int a0), (a0), -1)
WRAP_RET(xmlDtd *, xmlSAXParseDTD, (xmlSAXHandler * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlDoc *, xmlSAXParseDoc, (xmlSAXHandler * a0, const xmlChar * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlDoc *, xmlSAXParseEntity, (xmlSAXHandler * a0, const char * a1), (a0, a1), NULL)
WRAP_RET(xmlDoc *, xmlSAXParseFile, (xmlSAXHandler * a0, const char * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlDoc *, xmlSAXParseFileWithData, (xmlSAXHandler * a0, const char * a1, int a2, void * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlDoc *, xmlSAXParseMemory, (xmlSAXHandler * a0, const char * a1, int a2, int a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlDoc *, xmlSAXParseMemoryWithData, (xmlSAXHandler * a0, const char * a1, int a2, int a3, void * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(int, xmlSAXUserParseFile, (xmlSAXHandler * a0, void * a1, const char * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlSAXUserParseMemory, (xmlSAXHandler * a0, void * a1, const char * a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlSAXVersion, (xmlSAXHandler * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlSaveClose, (xmlSaveCtxt * a0), (a0), -1)
WRAP_RET(long int, xmlSaveDoc, (xmlSaveCtxt * a0, xmlDoc * a1), (a0, a1), ((long int){0}))
WRAP_RET(int, xmlSaveFile, (const char * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_RET(int, xmlSaveFileEnc, (const char * a0, xmlDoc * a1, const char * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlSaveFileTo, (xmlOutputBuffer * a0, xmlDoc * a1, const char * a2), (a0, a1, a2), -1)
WRAP_RET(xmlParserErrors, xmlSaveFinish, (xmlSaveCtxt * a0), (a0), ((xmlParserErrors){0}))
WRAP_RET(int, xmlSaveFlush, (xmlSaveCtxt * a0), (a0), -1)
WRAP_RET(int, xmlSaveFormatFile, (const char * a0, xmlDoc * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlSaveFormatFileEnc, (const char * a0, xmlDoc * a1, const char * a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlSaveFormatFileTo, (xmlOutputBuffer * a0, xmlDoc * a1, const char * a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlSaveSetAttrEscape, (xmlSaveCtxt * a0, xmlCharEncodingOutputFunc a1), (a0, a1), -1)
WRAP_RET(int, xmlSaveSetEscape, (xmlSaveCtxt * a0, xmlCharEncodingOutputFunc a1), (a0, a1), -1)
WRAP_RET(int, xmlSaveSetIndentString, (xmlSaveCtxt * a0, const char * a1), (a0, a1), -1)
WRAP_RET(xmlSaveCtxt *, xmlSaveToBuffer, (xmlBuffer * a0, const char * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlSaveCtxt *, xmlSaveToFd, (int a0, const char * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlSaveCtxt *, xmlSaveToFilename, (const char * a0, const char * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlSaveCtxt *, xmlSaveToIO, (xmlOutputWriteCallback a0, xmlOutputCloseCallback a1, void * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(long int, xmlSaveTree, (xmlSaveCtxt * a0, xmlNode * a1), (a0, a1), ((long int){0}))
WRAP_RET(xmlChar *, xmlSaveUri, (xmlURI * a0), (a0), NULL)
WRAP_RET(int, xmlSchemaCheckFacet, (xmlSchemaFacet * a0, xmlSchemaType * a1, xmlSchemaParserCtxt * a2, const xmlChar * a3), (a0, a1, a2, a3), -1)
WRAP_VOID(xmlSchemaCleanupTypes, (void), ())
WRAP_RET(xmlChar *, xmlSchemaCollapseString, (const xmlChar * a0), (a0), NULL)
WRAP_RET(int, xmlSchemaCompareValues, (xmlSchemaVal * a0, xmlSchemaVal * a1), (a0, a1), -1)
WRAP_RET(int, xmlSchemaCompareValuesWhtsp, (xmlSchemaVal * a0, xmlSchemaWhitespaceValueType a1, xmlSchemaVal * a2, xmlSchemaWhitespaceValueType a3), (a0, a1, a2, a3), -1)
WRAP_RET(xmlSchemaVal *, xmlSchemaCopyValue, (xmlSchemaVal * a0), (a0), NULL)
WRAP_VOID(xmlSchemaDump, (FILE * a0, xmlSchema * a1), (a0, a1))
WRAP_VOID(xmlSchemaFree, (xmlSchema * a0), (a0))
WRAP_VOID(xmlSchemaFreeFacet, (xmlSchemaFacet * a0), (a0))
WRAP_VOID(xmlSchemaFreeParserCtxt, (xmlSchemaParserCtxt * a0), (a0))
WRAP_VOID(xmlSchemaFreeType, (xmlSchemaType * a0), (a0))
WRAP_VOID(xmlSchemaFreeValidCtxt, (xmlSchemaValidCtxt * a0), (a0))
WRAP_VOID(xmlSchemaFreeValue, (xmlSchemaVal * a0), (a0))
WRAP_VOID(xmlSchemaFreeWildcard, (xmlSchemaWildcard * a0), (a0))
WRAP_RET(xmlSchemaType *, xmlSchemaGetBuiltInListSimpleTypeItemType, (xmlSchemaType * a0), (a0), NULL)
WRAP_RET(xmlSchemaType *, xmlSchemaGetBuiltInType, (xmlSchemaValType a0), (a0), NULL)
WRAP_RET(int, xmlSchemaGetCanonValue, (xmlSchemaVal * a0, const xmlChar ** a1), (a0, a1), -1)
WRAP_RET(int, xmlSchemaGetCanonValueWhtsp, (xmlSchemaVal * a0, const xmlChar ** a1, xmlSchemaWhitespaceValueType a2), (a0, a1, a2), -1)
WRAP_RET(long unsigned int, xmlSchemaGetFacetValueAsULong, (xmlSchemaFacet * a0), (a0), ((long unsigned int){0}))
WRAP_RET(int, xmlSchemaGetParserErrors, (xmlSchemaParserCtxt * a0, xmlSchemaValidityErrorFunc * a1, xmlSchemaValidityWarningFunc * a2, void ** a3), (a0, a1, a2, a3), -1)
WRAP_RET(xmlSchemaType *, xmlSchemaGetPredefinedType, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlSchemaValType, xmlSchemaGetValType, (xmlSchemaVal * a0), (a0), ((xmlSchemaValType){0}))
WRAP_RET(int, xmlSchemaGetValidErrors, (xmlSchemaValidCtxt * a0, xmlSchemaValidityErrorFunc * a1, xmlSchemaValidityWarningFunc * a2, void ** a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlSchemaInitTypes, (void), (), -1)
WRAP_RET(int, xmlSchemaIsBuiltInTypeFacet, (xmlSchemaType * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlSchemaIsValid, (xmlSchemaValidCtxt * a0), (a0), -1)
WRAP_RET(xmlSchemaParserCtxt *, xmlSchemaNewDocParserCtxt, (xmlDoc * a0), (a0), NULL)
WRAP_RET(xmlSchemaFacet *, xmlSchemaNewFacet, (void), (), NULL)
WRAP_RET(xmlSchemaParserCtxt *, xmlSchemaNewMemParserCtxt, (const char * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlSchemaVal *, xmlSchemaNewNOTATIONValue, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlSchemaParserCtxt *, xmlSchemaNewParserCtxt, (const char * a0), (a0), NULL)
WRAP_RET(xmlSchemaVal *, xmlSchemaNewQNameValue, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlSchemaVal *, xmlSchemaNewStringValue, (xmlSchemaValType a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlSchemaValidCtxt *, xmlSchemaNewValidCtxt, (xmlSchema * a0), (a0), NULL)
WRAP_RET(xmlSchema *, xmlSchemaParse, (xmlSchemaParserCtxt * a0), (a0), NULL)
WRAP_RET(xmlSchemaSAXPlugStruct *, xmlSchemaSAXPlug, (xmlSchemaValidCtxt * a0, xmlSAXHandler ** a1, void ** a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlSchemaSAXUnplug, (xmlSchemaSAXPlugStruct * a0), (a0), -1)
WRAP_VOID(xmlSchemaSetParserErrors, (xmlSchemaParserCtxt * a0, xmlSchemaValidityErrorFunc a1, xmlSchemaValidityWarningFunc a2, void * a3), (a0, a1, a2, a3))
WRAP_VOID(xmlSchemaSetParserStructuredErrors, (xmlSchemaParserCtxt * a0, xmlStructuredErrorFunc a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlSchemaSetResourceLoader, (xmlSchemaParserCtxt * a0, xmlResourceLoader a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlSchemaSetValidErrors, (xmlSchemaValidCtxt * a0, xmlSchemaValidityErrorFunc a1, xmlSchemaValidityWarningFunc a2, void * a3), (a0, a1, a2, a3))
WRAP_RET(int, xmlSchemaSetValidOptions, (xmlSchemaValidCtxt * a0, int a1), (a0, a1), -1)
WRAP_VOID(xmlSchemaSetValidStructuredErrors, (xmlSchemaValidCtxt * a0, xmlStructuredErrorFunc a1, void * a2), (a0, a1, a2))
WRAP_RET(int, xmlSchemaValPredefTypeNode, (xmlSchemaType * a0, const xmlChar * a1, xmlSchemaVal ** a2, xmlNode * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlSchemaValPredefTypeNodeNoNorm, (xmlSchemaType * a0, const xmlChar * a1, xmlSchemaVal ** a2, xmlNode * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlSchemaValidCtxtGetOptions, (xmlSchemaValidCtxt * a0), (a0), -1)
WRAP_RET(xmlParserCtxt *, xmlSchemaValidCtxtGetParserCtxt, (xmlSchemaValidCtxt * a0), (a0), NULL)
WRAP_RET(int, xmlSchemaValidateDoc, (xmlSchemaValidCtxt * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_RET(int, xmlSchemaValidateFacet, (xmlSchemaType * a0, xmlSchemaFacet * a1, const xmlChar * a2, xmlSchemaVal * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlSchemaValidateFacetWhtsp, (xmlSchemaFacet * a0, xmlSchemaWhitespaceValueType a1, xmlSchemaValType a2, const xmlChar * a3, xmlSchemaVal * a4, xmlSchemaWhitespaceValueType a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlSchemaValidateFile, (xmlSchemaValidCtxt * a0, const char * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlSchemaValidateLengthFacet, (xmlSchemaType * a0, xmlSchemaFacet * a1, const xmlChar * a2, xmlSchemaVal * a3, long unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlSchemaValidateLengthFacetWhtsp, (xmlSchemaFacet * a0, xmlSchemaValType a1, const xmlChar * a2, xmlSchemaVal * a3, long unsigned int * a4, xmlSchemaWhitespaceValueType a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlSchemaValidateListSimpleTypeFacet, (xmlSchemaFacet * a0, const xmlChar * a1, long unsigned int a2, long unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlSchemaValidateOneElement, (xmlSchemaValidCtxt * a0, xmlNode * a1), (a0, a1), -1)
WRAP_RET(int, xmlSchemaValidatePredefinedType, (xmlSchemaType * a0, const xmlChar * a1, xmlSchemaVal ** a2), (a0, a1, a2), -1)
WRAP_VOID(xmlSchemaValidateSetFilename, (xmlSchemaValidCtxt * a0, const char * a1), (a0, a1))
WRAP_VOID(xmlSchemaValidateSetLocator, (xmlSchemaValidCtxt * a0, xmlSchemaValidityLocatorFunc a1, void * a2), (a0, a1, a2))
WRAP_RET(int, xmlSchemaValidateStream, (xmlSchemaValidCtxt * a0, xmlParserInputBuffer * a1, xmlCharEncoding a2, const xmlSAXHandler * a3, void * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlSchemaValueAppend, (xmlSchemaVal * a0, xmlSchemaVal * a1), (a0, a1), -1)
WRAP_RET(int, xmlSchemaValueGetAsBoolean, (xmlSchemaVal * a0), (a0), -1)
WRAP_RET(const xmlChar *, xmlSchemaValueGetAsString, (xmlSchemaVal * a0), (a0), NULL)
WRAP_RET(xmlSchemaVal *, xmlSchemaValueGetNext, (xmlSchemaVal * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlSchemaWhiteSpaceReplace, (const xmlChar * a0), (a0), NULL)
WRAP_VOID(xmlSchematronFree, (xmlSchematron * a0), (a0))
WRAP_VOID(xmlSchematronFreeParserCtxt, (xmlSchematronParserCtxt * a0), (a0))
WRAP_VOID(xmlSchematronFreeValidCtxt, (xmlSchematronValidCtxt * a0), (a0))
WRAP_RET(xmlSchematronParserCtxt *, xmlSchematronNewDocParserCtxt, (xmlDoc * a0), (a0), NULL)
WRAP_RET(xmlSchematronParserCtxt *, xmlSchematronNewMemParserCtxt, (const char * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlSchematronParserCtxt *, xmlSchematronNewParserCtxt, (const char * a0), (a0), NULL)
WRAP_RET(xmlSchematronValidCtxt *, xmlSchematronNewValidCtxt, (xmlSchematron * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlSchematron *, xmlSchematronParse, (xmlSchematronParserCtxt * a0), (a0), NULL)
WRAP_VOID(xmlSchematronSetValidStructuredErrors, (xmlSchematronValidCtxt * a0, xmlStructuredErrorFunc a1, void * a2), (a0, a1, a2))
WRAP_RET(int, xmlSchematronValidateDoc, (xmlSchematronValidCtxt * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_RET(xmlNs *, xmlSearchNs, (xmlDoc * a0, xmlNode * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlNs *, xmlSearchNsByHref, (xmlDoc * a0, xmlNode * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_VOID(xmlSetBufferAllocationScheme, (xmlBufferAllocationScheme a0), (a0))
WRAP_VOID(xmlSetCompressMode, (int a0), (a0))
WRAP_VOID(xmlSetDocCompressMode, (xmlDoc * a0, int a1), (a0, a1))
WRAP_VOID(xmlSetExternalEntityLoader, (xmlExternalEntityLoader a0), (a0))
WRAP_VOID(xmlSetGenericErrorFunc, (void * a0, xmlGenericErrorFunc a1), (a0, a1))
WRAP_RET(int, xmlSetListDoc, (xmlNode * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_VOID(xmlSetNs, (xmlNode * a0, xmlNs * a1), (a0, a1))
WRAP_RET(xmlAttr *, xmlSetNsProp, (xmlNode * a0, xmlNs * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(xmlAttr *, xmlSetProp, (xmlNode * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_VOID(xmlSetStructuredErrorFunc, (void * a0, xmlStructuredErrorFunc a1), (a0, a1))
WRAP_RET(int, xmlSetTreeDoc, (xmlNode * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_VOID(xmlSetupParserForBuffer, (xmlParserCtxt * a0, const xmlChar * a1, const char * a2), (a0, a1, a2))
WRAP_RET(int, xmlSkipBlankChars, (xmlParserCtxt * a0), (a0), -1)
WRAP_VOID(xmlSnprintfElementContent, (char * a0, int a1, xmlElementContent * a2, int a3), (a0, a1, a2, a3))
WRAP_RET(xmlChar *, xmlSplitQName, (xmlParserCtxt * a0, const xmlChar * a1, xmlChar ** a2), (a0, a1, a2), NULL)
WRAP_RET(xmlChar *, xmlSplitQName2, (const xmlChar * a0, xmlChar ** a1), (a0, a1), NULL)
WRAP_RET(const xmlChar *, xmlSplitQName3, (const xmlChar * a0, int * a1), (a0, a1), NULL)
WRAP_VOID(xmlSprintfElementContent, (char * a0, xmlElementContent * a1, int a2), (a0, a1, a2))
WRAP_VOID(xmlStopParser, (xmlParserCtxt * a0), (a0))
WRAP_RET(int, xmlStrEqual, (const xmlChar * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlStrQEqual, (const xmlChar * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlStrVPrintf, (xmlChar * a0, int a1, const char * a2, va_list a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlStrcasecmp, (const xmlChar * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(const xmlChar *, xmlStrcasestr, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlChar *, xmlStrcat, (xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(const xmlChar *, xmlStrchr, (const xmlChar * a0, xmlChar a1), (a0, a1), NULL)
WRAP_RET(int, xmlStrcmp, (const xmlChar * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(xmlChar *, xmlStrdup, (const xmlChar * a0), (a0), NULL)
WRAP_RET(int, xmlStreamPop, (xmlStreamCtxt * a0), (a0), -1)
WRAP_RET(int, xmlStreamPush, (xmlStreamCtxt * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlStreamPushAttr, (xmlStreamCtxt * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlStreamPushNode, (xmlStreamCtxt * a0, const xmlChar * a1, const xmlChar * a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlStreamWantsAnyNode, (xmlStreamCtxt * a0), (a0), -1)
WRAP_RET(int, xmlStringCurrentChar, (xmlParserCtxt * a0, const xmlChar * a1, int * a2), (a0, a1, a2), -1)
WRAP_RET(xmlChar *, xmlStringDecodeEntities, (xmlParserCtxt * a0, const xmlChar * a1, int a2, xmlChar a3, xmlChar a4, xmlChar a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(xmlNode *, xmlStringGetNodeList, (const xmlDoc * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlChar *, xmlStringLenDecodeEntities, (xmlParserCtxt * a0, const xmlChar * a1, int a2, int a3, xmlChar a4, xmlChar a5, xmlChar a6), (a0, a1, a2, a3, a4, a5, a6), NULL)
WRAP_RET(xmlNode *, xmlStringLenGetNodeList, (const xmlDoc * a0, const xmlChar * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlStrlen, (const xmlChar * a0), (a0), -1)
WRAP_RET(int, xmlStrncasecmp, (const xmlChar * a0, const xmlChar * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(xmlChar *, xmlStrncat, (xmlChar * a0, const xmlChar * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(xmlChar *, xmlStrncatNew, (const xmlChar * a0, const xmlChar * a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlStrncmp, (const xmlChar * a0, const xmlChar * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(xmlChar *, xmlStrndup, (const xmlChar * a0, int a1), (a0, a1), NULL)
WRAP_RET(const xmlChar *, xmlStrstr, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlChar *, xmlStrsub, (const xmlChar * a0, int a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlSubstituteEntitiesDefault, (int a0), (a0), -1)
WRAP_RET(int, xmlSwitchEncoding, (xmlParserCtxt * a0, xmlCharEncoding a1), (a0, a1), -1)
WRAP_RET(int, xmlSwitchEncodingName, (xmlParserCtxt * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, xmlSwitchInputEncoding, (xmlParserCtxt * a0, xmlParserInput * a1, xmlCharEncodingHandler * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlSwitchToEncoding, (xmlParserCtxt * a0, xmlCharEncodingHandler * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextConcat, (xmlNode * a0, const xmlChar * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(xmlNode *, xmlTextMerge, (xmlNode * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(int, xmlTextReaderAttributeCount, (xmlTextReader * a0), (a0), -1)
WRAP_RET(xmlChar *, xmlTextReaderBaseUri, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(long int, xmlTextReaderByteConsumed, (xmlTextReader * a0), (a0), ((long int){0}))
WRAP_RET(int, xmlTextReaderClose, (xmlTextReader * a0), (a0), -1)
WRAP_RET(const xmlChar *, xmlTextReaderConstBaseUri, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlTextReaderConstEncoding, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlTextReaderConstLocalName, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlTextReaderConstName, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlTextReaderConstNamespaceUri, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlTextReaderConstPrefix, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlTextReaderConstString, (xmlTextReader * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(const xmlChar *, xmlTextReaderConstValue, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlTextReaderConstXmlLang, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(const xmlChar *, xmlTextReaderConstXmlVersion, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(xmlDoc *, xmlTextReaderCurrentDoc, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(xmlNode *, xmlTextReaderCurrentNode, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(int, xmlTextReaderDepth, (xmlTextReader * a0), (a0), -1)
WRAP_RET(xmlNode *, xmlTextReaderExpand, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlTextReaderGetAttribute, (xmlTextReader * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlChar *, xmlTextReaderGetAttributeNo, (xmlTextReader * a0, int a1), (a0, a1), NULL)
WRAP_RET(xmlChar *, xmlTextReaderGetAttributeNs, (xmlTextReader * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_VOID(xmlTextReaderGetErrorHandler, (xmlTextReader * a0, xmlTextReaderErrorFunc * a1, void ** a2), (a0, a1, a2))
WRAP_RET(const xmlError *, xmlTextReaderGetLastError, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(int, xmlTextReaderGetParserColumnNumber, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderGetParserLineNumber, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderGetParserProp, (xmlTextReader * a0, int a1), (a0, a1), -1)
WRAP_RET(xmlParserInputBuffer *, xmlTextReaderGetRemainder, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(int, xmlTextReaderHasAttributes, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderHasValue, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderIsDefault, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderIsEmptyElement, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderIsNamespaceDecl, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderIsValid, (xmlTextReader * a0), (a0), -1)
WRAP_RET(xmlChar *, xmlTextReaderLocalName, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlTextReaderLocatorBaseURI, (xmlTextReaderLocatorPtr a0), (a0), NULL)
WRAP_RET(int, xmlTextReaderLocatorLineNumber, (xmlTextReaderLocatorPtr a0), (a0), -1)
WRAP_RET(xmlChar *, xmlTextReaderLookupNamespace, (xmlTextReader * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(int, xmlTextReaderMoveToAttribute, (xmlTextReader * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextReaderMoveToAttributeNo, (xmlTextReader * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlTextReaderMoveToAttributeNs, (xmlTextReader * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextReaderMoveToElement, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderMoveToFirstAttribute, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderMoveToNextAttribute, (xmlTextReader * a0), (a0), -1)
WRAP_RET(xmlChar *, xmlTextReaderName, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlTextReaderNamespaceUri, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(int, xmlTextReaderNext, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderNextSibling, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderNodeType, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderNormalization, (xmlTextReader * a0), (a0), -1)
WRAP_RET(xmlChar *, xmlTextReaderPrefix, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(xmlNode *, xmlTextReaderPreserve, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(int, xmlTextReaderPreservePattern, (xmlTextReader * a0, const xmlChar * a1, const xmlChar ** a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextReaderQuoteChar, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderRead, (xmlTextReader * a0), (a0), -1)
WRAP_RET(int, xmlTextReaderReadAttributeValue, (xmlTextReader * a0), (a0), -1)
WRAP_RET(xmlChar *, xmlTextReaderReadInnerXml, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlTextReaderReadOuterXml, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(int, xmlTextReaderReadState, (xmlTextReader * a0), (a0), -1)
WRAP_RET(xmlChar *, xmlTextReaderReadString, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(int, xmlTextReaderRelaxNGSetSchema, (xmlTextReader * a0, xmlRelaxNG * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextReaderRelaxNGValidate, (xmlTextReader * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextReaderRelaxNGValidateCtxt, (xmlTextReader * a0, xmlRelaxNGValidCtxt * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextReaderSchemaValidate, (xmlTextReader * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextReaderSchemaValidateCtxt, (xmlTextReader * a0, xmlSchemaValidCtxt * a1, int a2), (a0, a1, a2), -1)
WRAP_VOID(xmlTextReaderSetErrorHandler, (xmlTextReader * a0, xmlTextReaderErrorFunc a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlTextReaderSetMaxAmplification, (xmlTextReader * a0, unsigned int a1), (a0, a1))
WRAP_RET(int, xmlTextReaderSetParserProp, (xmlTextReader * a0, int a1, int a2), (a0, a1, a2), -1)
WRAP_VOID(xmlTextReaderSetResourceLoader, (xmlTextReader * a0, xmlResourceLoader a1, void * a2), (a0, a1, a2))
WRAP_RET(int, xmlTextReaderSetSchema, (xmlTextReader * a0, xmlSchema * a1), (a0, a1), -1)
WRAP_VOID(xmlTextReaderSetStructuredErrorHandler, (xmlTextReader * a0, xmlStructuredErrorFunc a1, void * a2), (a0, a1, a2))
WRAP_RET(int, xmlTextReaderSetup, (xmlTextReader * a0, xmlParserInputBuffer * a1, const char * a2, const char * a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlTextReaderStandalone, (xmlTextReader * a0), (a0), -1)
WRAP_RET(xmlChar *, xmlTextReaderValue, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlTextReaderXmlLang, (xmlTextReader * a0), (a0), NULL)
WRAP_RET(int, xmlTextWriterClose, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterEndAttribute, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterEndCDATA, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterEndComment, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterEndDTD, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterEndDTDAttlist, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterEndDTDElement, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterEndDTDEntity, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterEndDocument, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterEndElement, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterEndPI, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterFlush, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterFullEndElement, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterSetIndent, (xmlTextWriter * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlTextWriterSetIndentString, (xmlTextWriter * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextWriterSetQuoteChar, (xmlTextWriter * a0, xmlChar a1), (a0, a1), -1)
WRAP_RET(int, xmlTextWriterStartAttribute, (xmlTextWriter * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextWriterStartAttributeNS, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterStartCDATA, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterStartComment, (xmlTextWriter * a0), (a0), -1)
WRAP_RET(int, xmlTextWriterStartDTD, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterStartDTDAttlist, (xmlTextWriter * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextWriterStartDTDElement, (xmlTextWriter * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextWriterStartDTDEntity, (xmlTextWriter * a0, int a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextWriterStartDocument, (xmlTextWriter * a0, const char * a1, const char * a2, const char * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterStartElement, (xmlTextWriter * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextWriterStartElementNS, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterStartPI, (xmlTextWriter * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextWriterWriteAttribute, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextWriterWriteAttributeNS, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, const xmlChar * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlTextWriterWriteBase64, (xmlTextWriter * a0, const char * a1, int a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterWriteBinHex, (xmlTextWriter * a0, const char * a1, int a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterWriteCDATA, (xmlTextWriter * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextWriterWriteComment, (xmlTextWriter * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextWriterWriteDTD, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, const xmlChar * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlTextWriterWriteDTDAttlist, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextWriterWriteDTDElement, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextWriterWriteDTDEntity, (xmlTextWriter * a0, int a1, const xmlChar * a2, const xmlChar * a3, const xmlChar * a4, const xmlChar * a5, const xmlChar * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, xmlTextWriterWriteDTDExternalEntity, (xmlTextWriter * a0, int a1, const xmlChar * a2, const xmlChar * a3, const xmlChar * a4, const xmlChar * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlTextWriterWriteDTDExternalEntityContents, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterWriteDTDInternalEntity, (xmlTextWriter * a0, int a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterWriteDTDNotation, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterWriteElement, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextWriterWriteElementNS, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, const xmlChar * a4), (a0, a1, a2, a3, a4), -1)
VFWD_INT(xmlTextWriterWriteFormatAttribute, (xmlTextWriter * a0, const xmlChar * a1, const char * a2, ...), (xmlTextWriter *, const xmlChar *, const char *), (a0, a1, "%s", b), a2)
VFWD_INT(xmlTextWriterWriteFormatAttributeNS, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, const char * a4, ...), (xmlTextWriter *, const xmlChar *, const xmlChar *, const xmlChar *, const char *), (a0, a1, a2, a3, "%s", b), a4)
VFWD_INT(xmlTextWriterWriteFormatCDATA, (xmlTextWriter * a0, const char * a1, ...), (xmlTextWriter *, const char *), (a0, "%s", b), a1)
VFWD_INT(xmlTextWriterWriteFormatComment, (xmlTextWriter * a0, const char * a1, ...), (xmlTextWriter *, const char *), (a0, "%s", b), a1)
VFWD_INT(xmlTextWriterWriteFormatDTD, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, const char * a4, ...), (xmlTextWriter *, const xmlChar *, const xmlChar *, const xmlChar *, const char *), (a0, a1, a2, a3, "%s", b), a4)
VFWD_INT(xmlTextWriterWriteFormatDTDAttlist, (xmlTextWriter * a0, const xmlChar * a1, const char * a2, ...), (xmlTextWriter *, const xmlChar *, const char *), (a0, a1, "%s", b), a2)
VFWD_INT(xmlTextWriterWriteFormatDTDElement, (xmlTextWriter * a0, const xmlChar * a1, const char * a2, ...), (xmlTextWriter *, const xmlChar *, const char *), (a0, a1, "%s", b), a2)
VFWD_INT(xmlTextWriterWriteFormatDTDInternalEntity, (xmlTextWriter * a0, int a1, const xmlChar * a2, const char * a3, ...), (xmlTextWriter *, int, const xmlChar *, const char *), (a0, a1, a2, "%s", b), a3)
VFWD_INT(xmlTextWriterWriteFormatElement, (xmlTextWriter * a0, const xmlChar * a1, const char * a2, ...), (xmlTextWriter *, const xmlChar *, const char *), (a0, a1, "%s", b), a2)
VFWD_INT(xmlTextWriterWriteFormatElementNS, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, const char * a4, ...), (xmlTextWriter *, const xmlChar *, const xmlChar *, const xmlChar *, const char *), (a0, a1, a2, a3, "%s", b), a4)
VFWD_INT(xmlTextWriterWriteFormatPI, (xmlTextWriter * a0, const xmlChar * a1, const char * a2, ...), (xmlTextWriter *, const xmlChar *, const char *), (a0, a1, "%s", b), a2)
VFWD_INT(xmlTextWriterWriteFormatRaw, (xmlTextWriter * a0, const char * a1, ...), (xmlTextWriter *, const char *), (a0, "%s", b), a1)
VFWD_INT(xmlTextWriterWriteFormatString, (xmlTextWriter * a0, const char * a1, ...), (xmlTextWriter *, const char *), (a0, "%s", b), a1)
WRAP_RET(int, xmlTextWriterWritePI, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextWriterWriteRaw, (xmlTextWriter * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextWriterWriteRawLen, (xmlTextWriter * a0, const xmlChar * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextWriterWriteString, (xmlTextWriter * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatAttribute, (xmlTextWriter * a0, const xmlChar * a1, const char * a2, va_list a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatAttributeNS, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, const char * a4, va_list a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatCDATA, (xmlTextWriter * a0, const char * a1, va_list a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatComment, (xmlTextWriter * a0, const char * a1, va_list a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatDTD, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, const char * a4, va_list a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatDTDAttlist, (xmlTextWriter * a0, const xmlChar * a1, const char * a2, va_list a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatDTDElement, (xmlTextWriter * a0, const xmlChar * a1, const char * a2, va_list a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatDTDInternalEntity, (xmlTextWriter * a0, int a1, const xmlChar * a2, const char * a3, va_list a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatElement, (xmlTextWriter * a0, const xmlChar * a1, const char * a2, va_list a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatElementNS, (xmlTextWriter * a0, const xmlChar * a1, const xmlChar * a2, const xmlChar * a3, const char * a4, va_list a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatPI, (xmlTextWriter * a0, const xmlChar * a1, const char * a2, va_list a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatRaw, (xmlTextWriter * a0, const char * a1, va_list a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlTextWriterWriteVFormatString, (xmlTextWriter * a0, const char * a1, va_list a2), (a0, a1, a2), -1)
WRAP_RET(xmlDeregisterNodeFunc, xmlThrDefDeregisterNodeDefault, (xmlDeregisterNodeFunc a0), (a0), ((xmlDeregisterNodeFunc){0}))
WRAP_RET(int, xmlThrDefDoValidityCheckingDefaultValue, (int a0), (a0), -1)
WRAP_RET(int, xmlThrDefGetWarningsDefaultValue, (int a0), (a0), -1)
WRAP_RET(int, xmlThrDefIndentTreeOutput, (int a0), (a0), -1)
WRAP_RET(int, xmlThrDefKeepBlanksDefaultValue, (int a0), (a0), -1)
WRAP_RET(int, xmlThrDefLineNumbersDefaultValue, (int a0), (a0), -1)
WRAP_RET(int, xmlThrDefLoadExtDtdDefaultValue, (int a0), (a0), -1)
WRAP_RET(xmlOutputBufferCreateFilenameFunc, xmlThrDefOutputBufferCreateFilenameDefault, (xmlOutputBufferCreateFilenameFunc a0), (a0), ((xmlOutputBufferCreateFilenameFunc){0}))
WRAP_RET(xmlParserInputBufferCreateFilenameFunc, xmlThrDefParserInputBufferCreateFilenameDefault, (xmlParserInputBufferCreateFilenameFunc a0), (a0), ((xmlParserInputBufferCreateFilenameFunc){0}))
WRAP_RET(int, xmlThrDefPedanticParserDefaultValue, (int a0), (a0), -1)
WRAP_RET(xmlRegisterNodeFunc, xmlThrDefRegisterNodeDefault, (xmlRegisterNodeFunc a0), (a0), ((xmlRegisterNodeFunc){0}))
WRAP_RET(int, xmlThrDefSaveNoEmptyTags, (int a0), (a0), -1)
WRAP_VOID(xmlThrDefSetGenericErrorFunc, (void * a0, xmlGenericErrorFunc a1), (a0, a1))
WRAP_VOID(xmlThrDefSetStructuredErrorFunc, (void * a0, xmlStructuredErrorFunc a1), (a0, a1))
WRAP_RET(int, xmlThrDefSubstituteEntitiesDefaultValue, (int a0), (a0), -1)
WRAP_RET(const char *, xmlThrDefTreeIndentString, (const char * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlURIEscape, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlURIEscapeStr, (const xmlChar * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(char *, xmlURIUnescapeString, (const char * a0, int a1, char * a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlUTF8Charcmp, (const xmlChar * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlUTF8Size, (const xmlChar * a0), (a0), -1)
WRAP_RET(int, xmlUTF8Strlen, (const xmlChar * a0), (a0), -1)
WRAP_RET(int, xmlUTF8Strloc, (const xmlChar * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(xmlChar *, xmlUTF8Strndup, (const xmlChar * a0, int a1), (a0, a1), NULL)
WRAP_RET(const xmlChar *, xmlUTF8Strpos, (const xmlChar * a0, int a1), (a0, a1), NULL)
WRAP_RET(int, xmlUTF8Strsize, (const xmlChar * a0, int a1), (a0, a1), -1)
WRAP_RET(xmlChar *, xmlUTF8Strsub, (const xmlChar * a0, int a1, int a2), (a0, a1, a2), NULL)
WRAP_RET(int, xmlUTF8ToIsolat1, (unsigned char * a0, int * a1, const unsigned char * a2, int * a3), (a0, a1, a2, a3), -1)
WRAP_VOID(xmlUnlinkNode, (xmlNode * a0), (a0))
WRAP_VOID(xmlUnlockLibrary, (void), ())
WRAP_RET(int, xmlUnsetNsProp, (xmlNode * a0, xmlNs * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlUnsetProp, (xmlNode * a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlValidBuildContentModel, (xmlValidCtxt * a0, xmlElement * a1), (a0, a1), -1)
WRAP_RET(xmlChar *, xmlValidCtxtNormalizeAttributeValue, (xmlValidCtxt * a0, xmlDoc * a1, xmlNode * a2, const xmlChar * a3, const xmlChar * a4), (a0, a1, a2, a3, a4), NULL)
WRAP_RET(int, xmlValidGetPotentialChildren, (xmlElementContent * a0, const xmlChar ** a1, int * a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlValidGetValidElements, (xmlNode * a0, xmlNode * a1, const xmlChar ** a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(xmlChar *, xmlValidNormalizeAttributeValue, (xmlDoc * a0, xmlNode * a1, const xmlChar * a2, const xmlChar * a3), (a0, a1, a2, a3), NULL)
WRAP_RET(int, xmlValidateAttributeDecl, (xmlValidCtxt * a0, xmlDoc * a1, xmlAttribute * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlValidateAttributeValue, (xmlAttributeType a0, const xmlChar * a1), (a0, a1), -1)
WRAP_RET(int, xmlValidateDocument, (xmlValidCtxt * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_RET(int, xmlValidateDocumentFinal, (xmlValidCtxt * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_RET(int, xmlValidateDtd, (xmlValidCtxt * a0, xmlDoc * a1, xmlDtd * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlValidateDtdFinal, (xmlValidCtxt * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_RET(int, xmlValidateElement, (xmlValidCtxt * a0, xmlDoc * a1, xmlNode * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlValidateElementDecl, (xmlValidCtxt * a0, xmlDoc * a1, xmlElement * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlValidateNCName, (const xmlChar * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlValidateNMToken, (const xmlChar * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlValidateName, (const xmlChar * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlValidateNameValue, (const xmlChar * a0), (a0), -1)
WRAP_RET(int, xmlValidateNamesValue, (const xmlChar * a0), (a0), -1)
WRAP_RET(int, xmlValidateNmtokenValue, (const xmlChar * a0), (a0), -1)
WRAP_RET(int, xmlValidateNmtokensValue, (const xmlChar * a0), (a0), -1)
WRAP_RET(int, xmlValidateNotationDecl, (xmlValidCtxt * a0, xmlDoc * a1, xmlNotation * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlValidateNotationUse, (xmlValidCtxt * a0, xmlDoc * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlValidateOneAttribute, (xmlValidCtxt * a0, xmlDoc * a1, xmlNode * a2, xmlAttr * a3, const xmlChar * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, xmlValidateOneElement, (xmlValidCtxt * a0, xmlDoc * a1, xmlNode * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlValidateOneNamespace, (xmlValidCtxt * a0, xmlDoc * a1, xmlNode * a2, const xmlChar * a3, xmlNs * a4, const xmlChar * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, xmlValidatePopElement, (xmlValidCtxt * a0, xmlDoc * a1, xmlNode * a2, const xmlChar * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlValidatePushCData, (xmlValidCtxt * a0, const xmlChar * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlValidatePushElement, (xmlValidCtxt * a0, xmlDoc * a1, xmlNode * a2, const xmlChar * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlValidateQName, (const xmlChar * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlValidateRoot, (xmlValidCtxt * a0, xmlDoc * a1), (a0, a1), -1)
WRAP_VOID(xmlXIncludeFreeContext, (xmlXIncludeCtxt * a0), (a0))
WRAP_RET(int, xmlXIncludeGetLastError, (xmlXIncludeCtxt * a0), (a0), -1)
WRAP_RET(xmlXIncludeCtxt *, xmlXIncludeNewContext, (xmlDoc * a0), (a0), NULL)
WRAP_RET(int, xmlXIncludeProcess, (xmlDoc * a0), (a0), -1)
WRAP_RET(int, xmlXIncludeProcessFlags, (xmlDoc * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlXIncludeProcessFlagsData, (xmlDoc * a0, int a1, void * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlXIncludeProcessNode, (xmlXIncludeCtxt * a0, xmlNode * a1), (a0, a1), -1)
WRAP_RET(int, xmlXIncludeProcessTree, (xmlNode * a0), (a0), -1)
WRAP_RET(int, xmlXIncludeProcessTreeFlags, (xmlNode * a0, int a1), (a0, a1), -1)
WRAP_RET(int, xmlXIncludeProcessTreeFlagsData, (xmlNode * a0, int a1, void * a2), (a0, a1, a2), -1)
WRAP_VOID(xmlXIncludeSetErrorHandler, (xmlXIncludeCtxt * a0, xmlStructuredErrorFunc a1, void * a2), (a0, a1, a2))
WRAP_RET(int, xmlXIncludeSetFlags, (xmlXIncludeCtxt * a0, int a1), (a0, a1), -1)
WRAP_VOID(xmlXIncludeSetResourceLoader, (xmlXIncludeCtxt * a0, xmlResourceLoader a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlXPathAddValues, (xmlXPathParserContext * a0), (a0))
WRAP_VOID(xmlXPathBooleanFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(double, xmlXPathCastBooleanToNumber, (int a0), (a0), ((double){0}))
WRAP_RET(xmlChar *, xmlXPathCastBooleanToString, (int a0), (a0), NULL)
WRAP_RET(int, xmlXPathCastNodeSetToBoolean, (xmlNodeSet * a0), (a0), -1)
WRAP_RET(double, xmlXPathCastNodeSetToNumber, (xmlNodeSet * a0), (a0), ((double){0}))
WRAP_RET(xmlChar *, xmlXPathCastNodeSetToString, (xmlNodeSet * a0), (a0), NULL)
WRAP_RET(double, xmlXPathCastNodeToNumber, (xmlNode * a0), (a0), ((double){0}))
WRAP_RET(xmlChar *, xmlXPathCastNodeToString, (xmlNode * a0), (a0), NULL)
WRAP_RET(int, xmlXPathCastNumberToBoolean, (double a0), (a0), -1)
WRAP_RET(xmlChar *, xmlXPathCastNumberToString, (double a0), (a0), NULL)
WRAP_RET(int, xmlXPathCastStringToBoolean, (const xmlChar * a0), (a0), -1)
WRAP_RET(double, xmlXPathCastStringToNumber, (const xmlChar * a0), (a0), ((double){0}))
WRAP_RET(int, xmlXPathCastToBoolean, (xmlXPathObject * a0), (a0), -1)
WRAP_RET(double, xmlXPathCastToNumber, (xmlXPathObject * a0), (a0), ((double){0}))
WRAP_RET(xmlChar *, xmlXPathCastToString, (xmlXPathObject * a0), (a0), NULL)
WRAP_VOID(xmlXPathCeilingFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(int, xmlXPathCmpNodes, (xmlNode * a0, xmlNode * a1), (a0, a1), -1)
WRAP_RET(int, xmlXPathCompareValues, (xmlXPathParserContext * a0, int a1, int a2), (a0, a1, a2), -1)
WRAP_RET(xmlXPathCompExpr *, xmlXPathCompile, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathCompiledEval, (xmlXPathCompExpr * a0, xmlXPathContext * a1), (a0, a1), NULL)
WRAP_RET(int, xmlXPathCompiledEvalToBoolean, (xmlXPathCompExpr * a0, xmlXPathContext * a1), (a0, a1), -1)
WRAP_VOID(xmlXPathConcatFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathContainsFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(int, xmlXPathContextSetCache, (xmlXPathContext * a0, int a1, int a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(xmlXPathObject *, xmlXPathConvertBoolean, (xmlXPathObject * a0), (a0), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathConvertNumber, (xmlXPathObject * a0), (a0), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathConvertString, (xmlXPathObject * a0), (a0), NULL)
WRAP_VOID(xmlXPathCountFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(xmlXPathCompExpr *, xmlXPathCtxtCompile, (xmlXPathContext * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_VOID(xmlXPathDebugDumpCompExpr, (FILE * a0, xmlXPathCompExpr * a1, int a2), (a0, a1, a2))
WRAP_VOID(xmlXPathDebugDumpObject, (FILE * a0, xmlXPathObject * a1, int a2), (a0, a1, a2))
WRAP_RET(xmlNodeSet *, xmlXPathDifference, (xmlNodeSet * a0, xmlNodeSet * a1), (a0, a1), NULL)
WRAP_RET(xmlNodeSet *, xmlXPathDistinct, (xmlNodeSet * a0), (a0), NULL)
WRAP_RET(xmlNodeSet *, xmlXPathDistinctSorted, (xmlNodeSet * a0), (a0), NULL)
WRAP_VOID(xmlXPathDivValues, (xmlXPathParserContext * a0), (a0))
WRAP_RET(int, xmlXPathEqualValues, (xmlXPathParserContext * a0), (a0), -1)
WRAP_VOID(xmlXPathErr, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(xmlXPathObject *, xmlXPathEval, (const xmlChar * a0, xmlXPathContext * a1), (a0, a1), NULL)
WRAP_VOID(xmlXPathEvalExpr, (xmlXPathParserContext * a0), (a0))
WRAP_RET(xmlXPathObject *, xmlXPathEvalExpression, (const xmlChar * a0, xmlXPathContext * a1), (a0, a1), NULL)
WRAP_RET(int, xmlXPathEvalPredicate, (xmlXPathContext * a0, xmlXPathObject * a1), (a0, a1), -1)
WRAP_RET(int, xmlXPathEvaluatePredicateResult, (xmlXPathParserContext * a0, xmlXPathObject * a1), (a0, a1), -1)
WRAP_VOID(xmlXPathFalseFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathFloorFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathFreeCompExpr, (xmlXPathCompExpr * a0), (a0))
WRAP_VOID(xmlXPathFreeContext, (xmlXPathContext * a0), (a0))
WRAP_VOID(xmlXPathFreeNodeSet, (xmlNodeSet * a0), (a0))
WRAP_VOID(xmlXPathFreeNodeSetList, (xmlXPathObject * a0), (a0))
WRAP_VOID(xmlXPathFreeObject, (xmlXPathObject * a0), (a0))
WRAP_VOID(xmlXPathFreeParserContext, (xmlXPathParserContext * a0), (a0))
WRAP_RET(xmlXPathFunction, xmlXPathFunctionLookup, (xmlXPathContext * a0, const xmlChar * a1), (a0, a1), ((xmlXPathFunction){0}))
WRAP_RET(xmlXPathFunction, xmlXPathFunctionLookupNS, (xmlXPathContext * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), ((xmlXPathFunction){0}))
WRAP_RET(int, xmlXPathHasSameNodes, (xmlNodeSet * a0, xmlNodeSet * a1), (a0, a1), -1)
WRAP_VOID(xmlXPathIdFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathInit, (void), ())
WRAP_RET(xmlNodeSet *, xmlXPathIntersection, (xmlNodeSet * a0, xmlNodeSet * a1), (a0, a1), NULL)
WRAP_RET(int, xmlXPathIsInf, (double a0), (a0), -1)
WRAP_RET(int, xmlXPathIsNaN, (double a0), (a0), -1)
WRAP_RET(int, xmlXPathIsNodeType, (const xmlChar * a0), (a0), -1)
WRAP_VOID(xmlXPathLangFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathLastFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(xmlNodeSet *, xmlXPathLeading, (xmlNodeSet * a0, xmlNodeSet * a1), (a0, a1), NULL)
WRAP_RET(xmlNodeSet *, xmlXPathLeadingSorted, (xmlNodeSet * a0, xmlNodeSet * a1), (a0, a1), NULL)
WRAP_VOID(xmlXPathLocalNameFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathModValues, (xmlXPathParserContext * a0), (a0))
WRAP_VOID(xmlXPathMultValues, (xmlXPathParserContext * a0), (a0))
WRAP_VOID(xmlXPathNamespaceURIFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(xmlXPathObject *, xmlXPathNewBoolean, (int a0), (a0), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathNewCString, (const char * a0), (a0), NULL)
WRAP_RET(xmlXPathContext *, xmlXPathNewContext, (xmlDoc * a0), (a0), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathNewFloat, (double a0), (a0), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathNewNodeSet, (xmlNode * a0), (a0), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathNewNodeSetList, (xmlNodeSet * a0), (a0), NULL)
WRAP_RET(xmlXPathParserContext *, xmlXPathNewParserContext, (const xmlChar * a0, xmlXPathContext * a1), (a0, a1), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathNewString, (const xmlChar * a0), (a0), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathNewValueTree, (xmlNode * a0), (a0), NULL)
WRAP_RET(xmlNode *, xmlXPathNextAncestor, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlXPathNextAncestorOrSelf, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlXPathNextAttribute, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlXPathNextChild, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlXPathNextDescendant, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlXPathNextDescendantOrSelf, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlXPathNextFollowing, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlXPathNextFollowingSibling, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlXPathNextNamespace, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlXPathNextParent, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlXPathNextPreceding, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlXPathNextPrecedingSibling, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNode *, xmlXPathNextSelf, (xmlXPathParserContext * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathNodeEval, (xmlNode * a0, const xmlChar * a1, xmlXPathContext * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlNodeSet *, xmlXPathNodeLeading, (xmlNodeSet * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNodeSet *, xmlXPathNodeLeadingSorted, (xmlNodeSet * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(int, xmlXPathNodeSetAdd, (xmlNodeSet * a0, xmlNode * a1), (a0, a1), -1)
WRAP_RET(int, xmlXPathNodeSetAddNs, (xmlNodeSet * a0, xmlNode * a1, xmlNs * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlXPathNodeSetAddUnique, (xmlNodeSet * a0, xmlNode * a1), (a0, a1), -1)
WRAP_RET(int, xmlXPathNodeSetContains, (xmlNodeSet * a0, xmlNode * a1), (a0, a1), -1)
WRAP_RET(xmlNodeSet *, xmlXPathNodeSetCreate, (xmlNode * a0), (a0), NULL)
WRAP_VOID(xmlXPathNodeSetDel, (xmlNodeSet * a0, xmlNode * a1), (a0, a1))
WRAP_VOID(xmlXPathNodeSetFreeNs, (xmlNs * a0), (a0))
WRAP_RET(xmlNodeSet *, xmlXPathNodeSetMerge, (xmlNodeSet * a0, xmlNodeSet * a1), (a0, a1), NULL)
WRAP_VOID(xmlXPathNodeSetRemove, (xmlNodeSet * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathNodeSetSort, (xmlNodeSet * a0), (a0))
WRAP_RET(xmlNodeSet *, xmlXPathNodeTrailing, (xmlNodeSet * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_RET(xmlNodeSet *, xmlXPathNodeTrailingSorted, (xmlNodeSet * a0, xmlNode * a1), (a0, a1), NULL)
WRAP_VOID(xmlXPathNormalizeFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(int, xmlXPathNotEqualValues, (xmlXPathParserContext * a0), (a0), -1)
WRAP_VOID(xmlXPathNotFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(const xmlChar *, xmlXPathNsLookup, (xmlXPathContext * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_VOID(xmlXPathNumberFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(xmlXPathObject *, xmlXPathObjectCopy, (xmlXPathObject * a0), (a0), NULL)
WRAP_RET(long int, xmlXPathOrderDocElems, (xmlDoc * a0), (a0), ((long int){0}))
WRAP_RET(xmlChar *, xmlXPathParseNCName, (xmlXPathParserContext * a0), (a0), NULL)
WRAP_RET(xmlChar *, xmlXPathParseName, (xmlXPathParserContext * a0), (a0), NULL)
WRAP_RET(int, xmlXPathPopBoolean, (xmlXPathParserContext * a0), (a0), -1)
WRAP_RET(void *, xmlXPathPopExternal, (xmlXPathParserContext * a0), (a0), NULL)
WRAP_RET(xmlNodeSet *, xmlXPathPopNodeSet, (xmlXPathParserContext * a0), (a0), NULL)
WRAP_RET(double, xmlXPathPopNumber, (xmlXPathParserContext * a0), (a0), ((double){0}))
WRAP_RET(xmlChar *, xmlXPathPopString, (xmlXPathParserContext * a0), (a0), NULL)
WRAP_VOID(xmlXPathPositionFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathRegisterAllFunctions, (xmlXPathContext * a0), (a0))
WRAP_RET(int, xmlXPathRegisterFunc, (xmlXPathContext * a0, const xmlChar * a1, xmlXPathFunction a2), (a0, a1, a2), -1)
WRAP_VOID(xmlXPathRegisterFuncLookup, (xmlXPathContext * a0, xmlXPathFuncLookupFunc a1, void * a2), (a0, a1, a2))
WRAP_RET(int, xmlXPathRegisterFuncNS, (xmlXPathContext * a0, const xmlChar * a1, const xmlChar * a2, xmlXPathFunction a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, xmlXPathRegisterNs, (xmlXPathContext * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), -1)
WRAP_RET(int, xmlXPathRegisterVariable, (xmlXPathContext * a0, const xmlChar * a1, xmlXPathObject * a2), (a0, a1, a2), -1)
WRAP_VOID(xmlXPathRegisterVariableLookup, (xmlXPathContext * a0, xmlXPathVariableLookupFunc a1, void * a2), (a0, a1, a2))
WRAP_RET(int, xmlXPathRegisterVariableNS, (xmlXPathContext * a0, const xmlChar * a1, const xmlChar * a2, xmlXPathObject * a3), (a0, a1, a2, a3), -1)
WRAP_VOID(xmlXPathRegisteredFuncsCleanup, (xmlXPathContext * a0), (a0))
WRAP_VOID(xmlXPathRegisteredNsCleanup, (xmlXPathContext * a0), (a0))
WRAP_VOID(xmlXPathRegisteredVariablesCleanup, (xmlXPathContext * a0), (a0))
WRAP_VOID(xmlXPathRoot, (xmlXPathParserContext * a0), (a0))
WRAP_VOID(xmlXPathRoundFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(int, xmlXPathSetContextNode, (xmlNode * a0, xmlXPathContext * a1), (a0, a1), -1)
WRAP_VOID(xmlXPathSetErrorHandler, (xmlXPathContext * a0, xmlStructuredErrorFunc a1, void * a2), (a0, a1, a2))
WRAP_VOID(xmlXPathStartsWithFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(double, xmlXPathStringEvalNumber, (const xmlChar * a0), (a0), ((double){0}))
WRAP_VOID(xmlXPathStringFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathStringLengthFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathSubValues, (xmlXPathParserContext * a0), (a0))
WRAP_VOID(xmlXPathSubstringAfterFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathSubstringBeforeFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathSubstringFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathSumFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_RET(xmlNodeSet *, xmlXPathTrailing, (xmlNodeSet * a0, xmlNodeSet * a1), (a0, a1), NULL)
WRAP_RET(xmlNodeSet *, xmlXPathTrailingSorted, (xmlNodeSet * a0, xmlNodeSet * a1), (a0, a1), NULL)
WRAP_VOID(xmlXPathTranslateFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathTrueFunction, (xmlXPathParserContext * a0, int a1), (a0, a1))
WRAP_VOID(xmlXPathValueFlipSign, (xmlXPathParserContext * a0), (a0))
WRAP_RET(xmlXPathObject *, xmlXPathValuePop, (xmlXPathParserContext * a0), (a0), NULL)
WRAP_RET(int, xmlXPathValuePush, (xmlXPathParserContext * a0, xmlXPathObject * a1), (a0, a1), -1)
WRAP_RET(xmlXPathObject *, xmlXPathVariableLookup, (xmlXPathContext * a0, const xmlChar * a1), (a0, a1), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathVariableLookupNS, (xmlXPathContext * a0, const xmlChar * a1, const xmlChar * a2), (a0, a1, a2), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathWrapCString, (char * a0), (a0), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathWrapExternal, (void * a0), (a0), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathWrapNodeSet, (xmlNodeSet * a0), (a0), NULL)
WRAP_RET(xmlXPathObject *, xmlXPathWrapString, (xmlChar * a0), (a0), NULL)
WRAP_VOID(xmlXPatherror, (xmlXPathParserContext * a0, const char * a1, int a2, int a3), (a0, a1, a2, a3))
WRAP_RET(xmlXPathObject *, xmlXPtrEval, (const xmlChar * a0, xmlXPathContext * a1), (a0, a1), NULL)
WRAP_RET(xmlXPathContext *, xmlXPtrNewContext, (xmlDoc * a0, xmlNode * a1, xmlNode * a2), (a0, a1, a2), NULL)

/* ============================================================
 * 可变参数导出函数（无法用函数指针转发，转 v-变体）
 * ============================================================ */
int xmlStrPrintf(xmlChar *buf, int len, const char *msg, ...) {
    static int (*fn)(xmlChar *, int, const char *, va_list) = NULL;
    va_list ap;
    int r;
    if (!fn) {
        if (!libxml2_dyn_libok()) return -1;
        *(void **)(&fn) = libxml2_sym("xmlStrVPrintf");
        if (!fn) {
            fprintf(stderr, "libxml2_dyn: dlsym failed: xmlStrVPrintf\n");
            return -1;
        }
    }
    va_start(ap, msg);
    r = fn(buf, len, msg, ap);
    va_end(ap);
    return r;
}


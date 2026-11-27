/* ============================================================
 * wolfssl_dyn.c — WOLFSSL 动态加载封装层（公开头文件 API 全量封装）
 *
 * 统计：导出函数 367 / 封装 349 / 跳过 18 / 数据符号 0 / 内联类型 34
 * 设计要点（与 rtdb/libxml2_dyn.c 一致）：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不链接目标库（只 -ldl）。
 *   2. 每个封装函数首次调用时才 dlsym 并缓存；未调用的 API 不解析。
 *   3. 编译期内联官方头类型（由 gcc -aux-info 核对签名），无目标库依赖。
 *   4. 加载顺序：显式路径 > WOLFSSL_DYN_LIB > LD_LIBRARY_PATH > soname > Nix > 平台默认。
 *   5. 数据符号从目标库同步值；const 符号以可写副本承载。
 *   6. 未封装符号（头文件未声明 / 宏或内联 / 未导出）见下方清单。
 * ============================================================ */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
/* ---------- 官方头文件内联类型 ---------- */
typedef pthread_mutex_t wolfSSL_Mutex;
typedef volatile int wolfSSL_Atomic_Int;
typedef volatile unsigned int wolfSSL_Atomic_Uint;
typedef struct wolfSSL_RefWithMutex {
    wolfSSL_Mutex mutex;
    int count;
} wolfSSL_RefWithMutex;
typedef int SOCKET_T;
typedef unsigned char byte;
typedef unsigned short word16;
typedef unsigned int word32;
typedef void *(*wolfSSL_Malloc_cb)(size_t size);
typedef void (*wolfSSL_Free_cb)(void *ptr);
typedef void *(*wolfSSL_Realloc_cb)(void *ptr, size_t size);
typedef struct COND_TYPE {
            pthread_mutex_t mutex;
            pthread_cond_t cond;
        } COND_TYPE;
typedef void* THREAD_RETURN;
typedef pthread_t THREAD_TYPE;
typedef THREAD_RETURN ( *THREAD_CB)(void* arg);
typedef struct WC_RNG WC_RNG;
typedef void (*wolfSSL_Logging_cb)(const int logLevel,
                                   const char *const logMessage);
typedef struct WOLFSSL WOLFSSL;
typedef struct WOLFSSL_SESSION WOLFSSL_SESSION;
typedef struct WOLFSSL_METHOD WOLFSSL_METHOD;
typedef struct WOLFSSL_CTX WOLFSSL_CTX;
typedef struct WOLFSSL_X509 WOLFSSL_X509;
typedef struct WOLFSSL_CERT_MANAGER WOLFSSL_CERT_MANAGER;
typedef struct WOLFSSL_X509_STORE_CTX WOLFSSL_X509_STORE_CTX;
struct iovec
  {
    void *iov_base;
    size_t iov_len;
  };
typedef int (*CallbackIORecv)(WOLFSSL *ssl, char *buf, int sz, void *ctx);
typedef int (*CallbackIOSend)(WOLFSSL *ssl, char *buf, int sz, void *ctx);
typedef struct WOLFSSL_CIPHER WOLFSSL_CIPHER;
typedef int (*VerifyCallback)(int, WOLFSSL_X509_STORE_CTX*);
typedef struct WOLFSSL_ALERT {
    int code;
    int level;
} WOLFSSL_ALERT;
typedef struct WOLFSSL_ALERT_HISTORY {
    WOLFSSL_ALERT last_rx;
    WOLFSSL_ALERT last_tx;
} WOLFSSL_ALERT_HISTORY;
typedef void (*CallbackCACache)(unsigned char* der, int sz, int type);
typedef int (*HandShakeDoneCb)(WOLFSSL* ssl, void*);
typedef int (*CallbackSniRecv)(WOLFSSL *ssl, int *ret, void* exArg);

/* ============================================================
 * 平台适配层：统一动态库 API 与默认库名
 * ============================================================ */
#ifdef _WIN32
 #include <windows.h>
 typedef HMODULE wolfssl_lib_handle_t;
 static wolfssl_lib_handle_t wolfssl_load_lib(const char *path) { return LoadLibraryA(path); }
 static void *wolfssl_get_sym(wolfssl_lib_handle_t h, const char *name) {
     return (void *)GetProcAddress(h, name);
 }
 static void wolfssl_free_lib(wolfssl_lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *wolfssl_lib_error(void) { return "LoadLibrary failed"; }
 #define WOLFSSL_PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *wolfssl_lib_handle_t;
 static wolfssl_lib_handle_t wolfssl_load_lib(const char *path) {
     return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
 }
 static void *wolfssl_get_sym(wolfssl_lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void wolfssl_free_lib(wolfssl_lib_handle_t h) { if (h) dlclose(h); }
 static const char *wolfssl_lib_error(void) { return dlerror(); }
 #define WOLFSSL_PATH_SEP ':'
#endif
/* ---------- 数据符号副本 ---------- */

/* 前向声明（LOADER 在下方定义 LIB_sym） */
void *wolfssl_sym(const char *name);

void wolfssl_dyn_sync_globals(void) {
    void *p;
    (void)p;
}

/* ============================================================
 * 全局状态与库加载
 * ============================================================ */
static wolfssl_lib_handle_t wolfssl_handle = NULL;
static int wolfssl_init_state = 0;

#define WOLFSSL_MAX_CANDIDATES 64
#define WOLFSSL_PATH_BUF_LEN   1024

static const char *wolfssl_defaults[] = {
"libwolfssl.so.46", "libwolfssl.so", "libwolfssl.dylib", "wolfssl.dll"
};

static int wolfssl_do_load(const char *library_path) {
    if (wolfssl_handle) return 0;
    static char bufs[WOLFSSL_MAX_CANDIDATES][WOLFSSL_PATH_BUF_LEN];
    const char *candidates[WOLFSSL_MAX_CANDIDATES];
    int n = 0, i;

    if (library_path && *library_path && n < WOLFSSL_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], WOLFSSL_PATH_BUF_LEN, "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    const char *explicit_lib = getenv("WOLFSSL_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < WOLFSSL_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], WOLFSSL_PATH_BUF_LEN, "%s", explicit_lib);
        candidates[n] = bufs[n]; n++;
    }
    const char *env_path = getenv(
#ifdef _WIN32
        "PATH"
#elif defined(__APPLE__)
        "DYLD_wolfsslRARY_PATH"
#else
        "LD_wolfsslRARY_PATH"
#endif
    );
    if (env_path && *env_path) {
        char *copy = strdup(env_path);
        if (copy) {
            char *p = copy;
            while (p && *p && n < WOLFSSL_MAX_CANDIDATES - 2) {
                char *sep = strchr(p, WOLFSSL_PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    for (i = 0; i < (int)(sizeof(wolfssl_defaults)/sizeof(wolfssl_defaults[0])); i++) {
                        if (n >= WOLFSSL_MAX_CANDIDATES - 2) break;
                        snprintf(bufs[n], WOLFSSL_PATH_BUF_LEN, "%s/%s", p, wolfssl_defaults[i]);
                        candidates[n] = bufs[n]; n++;
                    }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }
    for (i = 0; i < (int)(sizeof(wolfssl_defaults)/sizeof(wolfssl_defaults[0])) && n < WOLFSSL_MAX_CANDIDATES - 1; i++)
        candidates[n++] = wolfssl_defaults[i];
    const char *home = getenv("HOME");
    if (home && *home && n < WOLFSSL_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], WOLFSSL_PATH_BUF_LEN, "%s/.nix-profile/lib/%s", home, wolfssl_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    if (n < WOLFSSL_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], WOLFSSL_PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s", wolfssl_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        wolfssl_handle = wolfssl_load_lib(candidates[i]);
        if (wolfssl_handle) break;
    }
    if (!wolfssl_handle) {
        fprintf(stderr, "wolfssl_dyn: cannot load library (%s)\n", wolfssl_lib_error());
        return -1;
    }
    if (!wolfssl_get_sym(wolfssl_handle, "wolfCrypt_Cleanup")) {
        fprintf(stderr, "wolfssl_dyn: loaded library does not export %s\n", "wolfCrypt_Cleanup");
        wolfssl_free_lib(wolfssl_handle); wolfssl_handle = NULL;
        return -1;
    }
    fprintf(stderr, "wolfssl_dyn: loaded %d %s\n", i, candidates[i]);
    wolfssl_dyn_sync_globals();
    return 0;
}

void *wolfssl_sym(const char *name) { return wolfssl_handle ? wolfssl_get_sym(wolfssl_handle, name) : NULL; }

int wolfssl_libok(void) {
    if (wolfssl_init_state == 0)
        wolfssl_init_state = (wolfssl_do_load(NULL) == 0) ? 1 : -1;
    return (wolfssl_init_state == 1 && wolfssl_handle != NULL) ? 1 : 0;
}

int wolfssl_dyn_init(const char *library_path) {
    if (wolfssl_do_load(library_path) == 0) { wolfssl_init_state = 1; return 0; }
    if (wolfssl_init_state == 0) wolfssl_init_state = -1;
    return -1;
}

void wolfssl_dyn_cleanup(void) {
    /* 不卸载已成功加载的库（dlclose 不安全，且函数指针可能仍被持有） */
    if (!wolfssl_handle) wolfssl_init_state = 0;
    else if (wolfssl_init_state == -1) wolfssl_init_state = 0;
}

int wolfssl_dyn_has_symbol(const char *name) {
    if (!wolfssl_libok()) return 0;
    return wolfssl_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏：首次调用 dlsym 并缓存；失败打印 stderr 并返回安全值
 * ============================================================ */
#define WOLFSSL_WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!wolfssl_libok()) return fallback;                                \
        *(void **)(&fn) = wolfssl_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "wolfssl_dyn: dlsym failed: %s\n", #name);        \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define WOLFSSL_WRAP_VOID(name, params, args)                                     \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!wolfssl_libok()) return;                                         \
        *(void **)(&fn) = wolfssl_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "wolfssl_dyn: dlsym failed: %s\n", #name);        \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}
/* ---------- 全量函数封装 ---------- */
WOLFSSL_WRAP_RET(int, wolfCrypt_Cleanup, (), (), 0)
WOLFSSL_WRAP_RET(int, wolfCrypt_Init, (), (), 0)
WOLFSSL_WRAP_RET(int, wolfIO_Recv, (SOCKET_T a0, char * a1, int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfIO_Send, (SOCKET_T a0, char * a1, int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_AllowEncryptThenMac, (WOLFSSL * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_Atomic_Int_AddFetch, (volatile wolfSSL_Atomic_Int * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_Atomic_Int_CompareExchange, (volatile wolfSSL_Atomic_Int * a0, int * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_Atomic_Int_Exchange, (volatile wolfSSL_Atomic_Int * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_Atomic_Int_FetchAdd, (volatile wolfSSL_Atomic_Int * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_Atomic_Int_FetchSub, (volatile wolfSSL_Atomic_Int * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_VOID(wolfSSL_Atomic_Int_Init, (volatile wolfSSL_Atomic_Int * a0, int a1), (a0, a1))
WOLFSSL_WRAP_RET(int, wolfSSL_Atomic_Int_SubFetch, (volatile wolfSSL_Atomic_Int * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_Atomic_Ptr_CompareExchange, (void *volatile * a0, void ** a1, void * a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(unsigned int, wolfSSL_Atomic_Uint_AddFetch, (volatile wolfSSL_Atomic_Uint * a0, unsigned int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_Atomic_Uint_CompareExchange, (volatile wolfSSL_Atomic_Uint * a0, unsigned int * a1, unsigned int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(unsigned int, wolfSSL_Atomic_Uint_FetchAdd, (volatile wolfSSL_Atomic_Uint * a0, unsigned int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(unsigned int, wolfSSL_Atomic_Uint_FetchSub, (volatile wolfSSL_Atomic_Uint * a0, unsigned int a1), (a0, a1), 0)
WOLFSSL_WRAP_VOID(wolfSSL_Atomic_Uint_Init, (volatile wolfSSL_Atomic_Uint * a0, unsigned int a1), (a0, a1))
WOLFSSL_WRAP_RET(unsigned int, wolfSSL_Atomic_Uint_SubFetch, (volatile wolfSSL_Atomic_Uint * a0, unsigned int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(word32, wolfSSL_CIPHER_get_id, (const WOLFSSL_CIPHER * a0), (a0), ((word32)0))
WOLFSSL_WRAP_RET(const char *, wolfSSL_CIPHER_get_name, (const WOLFSSL_CIPHER * a0), (a0), NULL)
WOLFSSL_WRAP_RET(const char *, wolfSSL_CIPHER_get_version, (const WOLFSSL_CIPHER * a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_AllowEncryptThenMac, (WOLFSSL_CTX * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_DisableExtendedMasterSecret, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_EnableExtendedMasterSecret, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(WOLFSSL_CERT_MANAGER *, wolfSSL_CTX_GetCertManager, (WOLFSSL_CTX * a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_GetDevId, (WOLFSSL_CTX * a0, WOLFSSL * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(void *, wolfSSL_CTX_GetHeap, (WOLFSSL_CTX * a0, WOLFSSL * a1), (a0, a1), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_GetObjectSize, (), (), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_RequireExtendedMasterSecret, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_VOID(wolfSSL_CTX_SNI_SetOptions, (WOLFSSL_CTX * a0, unsigned char a1, unsigned char a2), (a0, a1, a2))
WOLFSSL_WRAP_VOID(wolfSSL_CTX_SetCACb, (WOLFSSL_CTX * a0, CallbackCACache a1), (a0, a1))
WOLFSSL_WRAP_VOID(wolfSSL_CTX_SetCertCbCtx, (WOLFSSL_CTX * a0, void * a1), (a0, a1))
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_SetDevId, (WOLFSSL_CTX * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_VOID(wolfSSL_CTX_SetIORecv, (WOLFSSL_CTX * a0, CallbackIORecv a1), (a0, a1))
WOLFSSL_WRAP_VOID(wolfSSL_CTX_SetIOSend, (WOLFSSL_CTX * a0, CallbackIOSend a1), (a0, a1))
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_SetMaxDhKey_Sz, (WOLFSSL_CTX * a0, word16 a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_SetMinDhKey_Sz, (WOLFSSL_CTX * a0, word16 a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_SetMinEccKey_Sz, (WOLFSSL_CTX * a0, short int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_SetMinRsaKey_Sz, (WOLFSSL_CTX * a0, short int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_SetMinVersion, (WOLFSSL_CTX * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_SetTmpDH, (WOLFSSL_CTX * a0, const unsigned char * a1, int a2, const unsigned char * a3, int a4), (a0, a1, a2, a3, a4), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_SetTmpDH_buffer, (WOLFSSL_CTX * a0, const unsigned char * a1, long int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_SetTmpDH_file, (WOLFSSL_CTX * a0, const char * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_SetTmpEC_DHE_Sz, (WOLFSSL_CTX * a0, word16 a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_UnloadCAs, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_UnloadIntermediateCerts, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_UseClientSuites, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_UseSNI, (WOLFSSL_CTX * a0, unsigned char a1, const void * a2, short unsigned int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_UseSupportedCurve, (WOLFSSL_CTX * a0, word16 a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_add_session, (WOLFSSL_CTX * a0, WOLFSSL_SESSION * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_check_private_key, (const WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_clear_group_messages, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(long int, wolfSSL_CTX_clear_options, (WOLFSSL_CTX * a0, long int a1), (a0, a1), ((long int)0))
WOLFSSL_WRAP_VOID(wolfSSL_CTX_flush_sessions, (WOLFSSL_CTX * a0, long int a1), (a0, a1))
WOLFSSL_WRAP_VOID(wolfSSL_CTX_free, (WOLFSSL_CTX * a0), (a0))
WOLFSSL_WRAP_RET(long int, wolfSSL_CTX_get_options, (WOLFSSL_CTX * a0), (a0), ((long int)0))
WOLFSSL_WRAP_RET(long int, wolfSSL_CTX_get_verify_depth, (WOLFSSL_CTX * a0), (a0), ((long int)0))
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_load_system_CA_certs, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_load_verify_buffer, (WOLFSSL_CTX * a0, const unsigned char * a1, long int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_load_verify_buffer_ex, (WOLFSSL_CTX * a0, const unsigned char * a1, long int a2, int a3, int a4, word32 a5), (a0, a1, a2, a3, a4, a5), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_load_verify_chain_buffer_format, (WOLFSSL_CTX * a0, const unsigned char * a1, long int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_load_verify_locations, (WOLFSSL_CTX * a0, const char * a1, const char * a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_load_verify_locations_compat, (WOLFSSL_CTX * a0, const char * a1, const char * a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_load_verify_locations_ex, (WOLFSSL_CTX * a0, const char * a1, const char * a2, word32 a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_mutual_auth, (WOLFSSL_CTX * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(WOLFSSL_CTX *, wolfSSL_CTX_new, (WOLFSSL_METHOD * a0), (a0), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_CTX *, wolfSSL_CTX_new_ex, (WOLFSSL_METHOD * a0, void * a1), (a0, a1), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_no_dhe_psk, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_no_ticket_TLSv13, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_only_dhe_psk, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_require_psk, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_set_cipher_list, (WOLFSSL_CTX * a0, const char * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_set_group_messages, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_set_groups, (WOLFSSL_CTX * a0, int * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(long int, wolfSSL_CTX_set_options, (WOLFSSL_CTX * a0, long int a1), (a0, a1), ((long int)0))
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_set_servername_arg, (WOLFSSL_CTX * a0, void * a1), (a0, a1), 0)
WOLFSSL_WRAP_VOID(wolfSSL_CTX_set_servername_callback, (WOLFSSL_CTX * a0, CallbackSniRecv a1), (a0, a1))
WOLFSSL_WRAP_RET(long int, wolfSSL_CTX_set_session_cache_mode, (WOLFSSL_CTX * a0, long int a1), (a0, a1), ((long int)0))
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_set_timeout, (WOLFSSL_CTX * a0, unsigned int a1), (a0, a1), 0)
WOLFSSL_WRAP_VOID(wolfSSL_CTX_set_verify, (WOLFSSL_CTX * a0, int a1, VerifyCallback a2), (a0, a1, a2))
WOLFSSL_WRAP_VOID(wolfSSL_CTX_set_verify_depth, (WOLFSSL_CTX * a0, int a1), (a0, a1))
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_up_ref, (WOLFSSL_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_use_PrivateKey_buffer, (WOLFSSL_CTX * a0, const unsigned char * a1, long int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_use_PrivateKey_file, (WOLFSSL_CTX * a0, const char * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_use_certificate_buffer, (WOLFSSL_CTX * a0, const unsigned char * a1, long int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_use_certificate_chain_buffer, (WOLFSSL_CTX * a0, const unsigned char * a1, long int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_use_certificate_chain_buffer_format, (WOLFSSL_CTX * a0, const unsigned char * a1, long int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_use_certificate_chain_file, (WOLFSSL_CTX * a0, const char * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_use_certificate_chain_file_format, (WOLFSSL_CTX * a0, const char * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CTX_use_certificate_file, (WOLFSSL_CTX * a0, const char * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertDebugging_OFF, (), (), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertDebugging_ON, (), (), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerDisableCRL, (WOLFSSL_CERT_MANAGER * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerDisableOCSP, (WOLFSSL_CERT_MANAGER * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerDisableOCSPMustStaple, (WOLFSSL_CERT_MANAGER * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerDisableOCSPStapling, (WOLFSSL_CERT_MANAGER * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerEnableCRL, (WOLFSSL_CERT_MANAGER * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerEnableOCSP, (WOLFSSL_CERT_MANAGER * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerEnableOCSPMustStaple, (WOLFSSL_CERT_MANAGER * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerEnableOCSPStapling, (WOLFSSL_CERT_MANAGER * a0), (a0), 0)
WOLFSSL_WRAP_VOID(wolfSSL_CertManagerFree, (WOLFSSL_CERT_MANAGER * a0), (a0))
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerLoadCA, (WOLFSSL_CERT_MANAGER * a0, const char * a1, const char * a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerLoadCABuffer, (WOLFSSL_CERT_MANAGER * a0, const unsigned char * a1, long int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerLoadCABufferType, (WOLFSSL_CERT_MANAGER * a0, const unsigned char * a1, long int a2, int a3, int a4, word32 a5, int a6), (a0, a1, a2, a3, a4, a5, a6), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerLoadCABuffer_ex, (WOLFSSL_CERT_MANAGER * a0, const unsigned char * a1, long int a2, int a3, int a4, word32 a5), (a0, a1, a2, a3, a4, a5), 0)
WOLFSSL_WRAP_RET(WOLFSSL_CERT_MANAGER *, wolfSSL_CertManagerNew, (), (), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_CERT_MANAGER *, wolfSSL_CertManagerNew_ex, (void * a0), (a0), NULL)
WOLFSSL_WRAP_VOID(wolfSSL_CertManagerSetVerify, (WOLFSSL_CERT_MANAGER * a0, VerifyCallback a1), (a0, a1))
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerUnloadCAs, (WOLFSSL_CERT_MANAGER * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerUnloadIntermediateCerts, (WOLFSSL_CERT_MANAGER * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerUnloadTypeCerts, (WOLFSSL_CERT_MANAGER * a0, byte a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerVerify, (WOLFSSL_CERT_MANAGER * a0, const char * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManagerVerifyBuffer, (WOLFSSL_CERT_MANAGER * a0, const unsigned char * a1, long int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CertManager_up_ref, (WOLFSSL_CERT_MANAGER * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_Cleanup, (), (), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CondEnd, (COND_TYPE * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CondFree, (COND_TYPE * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CondInit, (COND_TYPE * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CondSignal, (COND_TYPE * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CondStart, (COND_TYPE * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_CondWait, (COND_TYPE * a0), (a0), 0)
WOLFSSL_WRAP_VOID(wolfSSL_Debugging_OFF, (), ())
WOLFSSL_WRAP_RET(int, wolfSSL_Debugging_ON, (), (), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_DeriveTlsKeys, (unsigned char * a0, word32 a1, const unsigned char * a2, word32 a3, const unsigned char * a4, const unsigned char * a5, int a6, int a7), (a0, a1, a2, a3, a4, a5, a6, a7), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_DisableExtendedMasterSecret, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(char *, wolfSSL_ERR_error_string, (long unsigned int a0, char * a1), (a0, a1), NULL)
WOLFSSL_WRAP_VOID(wolfSSL_ERR_error_string_n, (long unsigned int a0, char * a1, long unsigned int a2), (a0, a1, a2))
WOLFSSL_WRAP_RET(const char *, wolfSSL_ERR_func_error_string, (long unsigned int a0), (a0), NULL)
WOLFSSL_WRAP_RET(const char *, wolfSSL_ERR_lib_error_string, (long unsigned int a0), (a0), NULL)
WOLFSSL_WRAP_VOID(wolfSSL_ERR_print_errors_fp, (FILE * a0, int a1), (a0, a1))
WOLFSSL_WRAP_RET(const char *, wolfSSL_ERR_reason_error_string, (long unsigned int a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_EnableExtendedMasterSecret, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_VOID(wolfSSL_Free, (void * a0), (a0))
WOLFSSL_WRAP_VOID(wolfSSL_FreeArrays, (WOLFSSL * a0), (a0))
WOLFSSL_WRAP_RET(int, wolfSSL_FreeHandshakeResources, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_GetAllocators, (wolfSSL_Malloc_cb * a0, wolfSSL_Free_cb * a1, wolfSSL_Realloc_cb * a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_GetDhKey_Sz, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_GetHmacMaxSize, (), (), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_GetHmacType, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(void *, wolfSSL_GetIOReadCtx, (WOLFSSL * a0), (a0), NULL)
WOLFSSL_WRAP_RET(void *, wolfSSL_GetIOWriteCtx, (WOLFSSL * a0), (a0), NULL)
WOLFSSL_WRAP_RET(wolfSSL_Logging_cb, wolfSSL_GetLoggingCb, (), (), ((wolfSSL_Logging_cb)0))
WOLFSSL_WRAP_RET(const unsigned char *, wolfSSL_GetMacSecret, (WOLFSSL * a0, int a1), (a0, a1), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_GetMaxOutputSize, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_GetObjectSize, (), (), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_GetOutputSize, (WOLFSSL * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(WC_RNG *, wolfSSL_GetRNG, (WOLFSSL * a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_GetSide, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_GetVersion, (const WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_Init, (), (), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_JoinThread, (THREAD_TYPE a0), (a0), 0)
WOLFSSL_WRAP_VOID(wolfSSL_KeepArrays, (WOLFSSL * a0), (a0))
WOLFSSL_WRAP_RET(int, wolfSSL_KeepHandshakeResources, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_METHOD_GetObjectSize, (), (), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_MakeTlsExtendedMasterSecret, (unsigned char * a0, word32 a1, const unsigned char * a2, word32 a3, const unsigned char * a4, word32 a5, int a6, int a7), (a0, a1, a2, a3, a4, a5, a6, a7), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_MakeTlsMasterSecret, (unsigned char * a0, word32 a1, const unsigned char * a2, word32 a3, const unsigned char * a4, const unsigned char * a5, int a6, int a7), (a0, a1, a2, a3, a4, a5, a6, a7), 0)
WOLFSSL_WRAP_RET(void *, wolfSSL_Malloc, (size_t a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_NewThread, (THREAD_TYPE * a0, THREAD_CB a1, void * a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_NewThreadNoJoin, (THREAD_CB a0, void * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_NoKeyShares, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(void *, wolfSSL_Realloc, (void * a0, size_t a1), (a0, a1), NULL)
WOLFSSL_WRAP_VOID(wolfSSL_RefWithMutexDec, (wolfSSL_RefWithMutex * a0, int * a1, int * a2), (a0, a1, a2))
WOLFSSL_WRAP_VOID(wolfSSL_RefWithMutexDec2, (wolfSSL_RefWithMutex * a0, int * a1, int * a2), (a0, a1, a2))
WOLFSSL_WRAP_VOID(wolfSSL_RefWithMutexDec_IfEquals, (wolfSSL_RefWithMutex * a0, int a1, int * a2, int * a3), (a0, a1, a2, a3))
WOLFSSL_WRAP_VOID(wolfSSL_RefWithMutexFree, (wolfSSL_RefWithMutex * a0), (a0))
WOLFSSL_WRAP_VOID(wolfSSL_RefWithMutexInc, (wolfSSL_RefWithMutex * a0, int * a1), (a0, a1))
WOLFSSL_WRAP_VOID(wolfSSL_RefWithMutexInc2, (wolfSSL_RefWithMutex * a0, int * a1, int * a2), (a0, a1, a2))
WOLFSSL_WRAP_VOID(wolfSSL_RefWithMutexInc_IfAtLeast, (wolfSSL_RefWithMutex * a0, int a1, int * a2, int * a3), (a0, a1, a2, a3))
WOLFSSL_WRAP_VOID(wolfSSL_RefWithMutexInit, (wolfSSL_RefWithMutex * a0, int * a1), (a0, a1))
WOLFSSL_WRAP_RET(int, wolfSSL_RefWithMutexLock, (wolfSSL_RefWithMutex * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_RefWithMutexUnlock, (wolfSSL_RefWithMutex * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_RequireExtendedMasterSecret, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(const char *, wolfSSL_SESSION_CIPHER_get_name, (const WOLFSSL_SESSION * a0), (a0), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_SESSION *, wolfSSL_SESSION_dup, (WOLFSSL_SESSION * a0), (a0), NULL)
WOLFSSL_WRAP_VOID(wolfSSL_SESSION_free, (WOLFSSL_SESSION * a0), (a0))
WOLFSSL_WRAP_RET(WOLFSSL_SESSION *, wolfSSL_SESSION_new, (), (), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_SESSION *, wolfSSL_SESSION_new_ex, (void * a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_SESSION_up_ref, (WOLFSSL_SESSION * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SNI_GetFromBuffer, (const unsigned char * a0, unsigned int a1, unsigned char a2, unsigned char * a3, unsigned int * a4), (a0, a1, a2, a3, a4), 0)
WOLFSSL_WRAP_RET(short unsigned int, wolfSSL_SNI_GetRequest, (WOLFSSL * a0, unsigned char a1, void ** a2), (a0, a1, a2), ((short unsigned int)0))
WOLFSSL_WRAP_VOID(wolfSSL_SNI_SetOptions, (WOLFSSL * a0, unsigned char a1, unsigned char a2), (a0, a1, a2))
WOLFSSL_WRAP_RET(unsigned char, wolfSSL_SNI_Status, (WOLFSSL * a0, unsigned char a1), (a0, a1), 0)
WOLFSSL_WRAP_VOID(wolfSSL_SSLDisableRead, (WOLFSSL * a0), (a0))
WOLFSSL_WRAP_VOID(wolfSSL_SSLEnableRead, (WOLFSSL * a0), (a0))
WOLFSSL_WRAP_VOID(wolfSSL_SSLSetIORecv, (WOLFSSL * a0, CallbackIORecv a1), (a0, a1))
WOLFSSL_WRAP_VOID(wolfSSL_SSLSetIOSend, (WOLFSSL * a0, CallbackIOSend a1), (a0, a1))
WOLFSSL_WRAP_RET(int, wolfSSL_SSL_CTX_remove_session, (WOLFSSL_CTX * a0, WOLFSSL_SESSION * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SendUserCanceled, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SessionIsSetup, (WOLFSSL_SESSION * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetAllocators, (wolfSSL_Malloc_cb a0, wolfSSL_Free_cb a1, wolfSSL_Realloc_cb a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_VOID(wolfSSL_SetCertCbCtx, (WOLFSSL * a0, void * a1), (a0, a1))
WOLFSSL_WRAP_RET(int, wolfSSL_SetDevId, (WOLFSSL * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetEnableDhKeyTest, (WOLFSSL * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetHsDoneCb, (WOLFSSL * a0, HandShakeDoneCb a1, void * a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_VOID(wolfSSL_SetIOReadCtx, (WOLFSSL * a0, void * a1), (a0, a1))
WOLFSSL_WRAP_VOID(wolfSSL_SetIOReadFlags, (WOLFSSL * a0, int a1), (a0, a1))
WOLFSSL_WRAP_VOID(wolfSSL_SetIOWriteCtx, (WOLFSSL * a0, void * a1), (a0, a1))
WOLFSSL_WRAP_VOID(wolfSSL_SetIOWriteFlags, (WOLFSSL * a0, int a1), (a0, a1))
WOLFSSL_WRAP_RET(int, wolfSSL_SetLoggingCb, (wolfSSL_Logging_cb a0), (a0), 0)
WOLFSSL_WRAP_VOID(wolfSSL_SetLoggingPrefix, (const char * a0), (a0))
WOLFSSL_WRAP_RET(int, wolfSSL_SetMaxDhKey_Sz, (WOLFSSL * a0, word16 a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetMinDhKey_Sz, (WOLFSSL * a0, word16 a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetMinEccKey_Sz, (WOLFSSL * a0, short int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetMinRsaKey_Sz, (WOLFSSL * a0, short int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetMinVersion, (WOLFSSL * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetServerID, (WOLFSSL * a0, const unsigned char * a1, int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetTlsHmacInner, (WOLFSSL * a0, byte * a1, word32 a2, int a3, int a4), (a0, a1, a2, a3, a4), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetTmpDH, (WOLFSSL * a0, const unsigned char * a1, int a2, const unsigned char * a3, int a4), (a0, a1, a2, a3, a4), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetTmpDH_buffer, (WOLFSSL * a0, const unsigned char * a1, long int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetTmpDH_file, (WOLFSSL * a0, const char * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetTmpEC_DHE_Sz, (WOLFSSL * a0, word16 a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_SetVersion, (WOLFSSL * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_UnloadCertsKeys, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_UseClientSuites, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_UseKeyShare, (WOLFSSL * a0, word16 a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_UseSNI, (WOLFSSL * a0, unsigned char a1, const void * a2, short unsigned int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_UseSupportedCurve, (WOLFSSL * a0, word16 a1), (a0, a1), 0)
WOLFSSL_WRAP_VOID(wolfSSL_X509_STORE_CTX_free, (WOLFSSL_X509_STORE_CTX * a0), (a0))
WOLFSSL_WRAP_RET(int, wolfSSL_X509_STORE_CTX_get_error, (WOLFSSL_X509_STORE_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_X509_STORE_CTX_get_error_depth, (WOLFSSL_X509_STORE_CTX * a0), (a0), 0)
WOLFSSL_WRAP_RET(WOLFSSL_X509_STORE_CTX *, wolfSSL_X509_STORE_CTX_new_ex, (void * a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_X509_check_host, (WOLFSSL_X509 * a0, const char * a1, size_t a2, unsigned int a3, char ** a4), (a0, a1, a2, a3, a4), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_X509_check_ip_asc, (WOLFSSL_X509 * a0, const char * a1, unsigned int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_accept, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_accept_TLSv13, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_check_domain_name, (WOLFSSL * a0, const char * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_check_ip_address, (WOLFSSL * a0, const char * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_clear, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_clear_group_messages, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_connect, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_connect_TLSv13, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_connect_cert, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_dtls, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_dtls_get0_peer, (WOLFSSL * a0, const void ** a1, unsigned int * a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_dtls_get_peer, (WOLFSSL * a0, void * a1, unsigned int * a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_dtls_get_using_nonblock, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_dtls_set_peer, (WOLFSSL * a0, void * a1, unsigned int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_VOID(wolfSSL_dtls_set_using_nonblock, (WOLFSSL * a0, int a1), (a0, a1))
WOLFSSL_WRAP_VOID(wolfSSL_flush_sessions, (WOLFSSL_CTX * a0, long int a1), (a0, a1))
WOLFSSL_WRAP_VOID(wolfSSL_free, (WOLFSSL * a0), (a0))
WOLFSSL_WRAP_RET(WOLFSSL_SESSION *, wolfSSL_get1_session, (WOLFSSL * a0), (a0), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_CTX *, wolfSSL_get_SSL_CTX, (const WOLFSSL * a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_get_alert_history, (WOLFSSL * a0, WOLFSSL_ALERT_HISTORY * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(const char *, wolfSSL_get_cipher, (WOLFSSL * a0), (a0), NULL)
WOLFSSL_WRAP_RET(const WOLFSSL_CIPHER *, wolfSSL_get_cipher_by_value, (word16 a0), (a0), NULL)
WOLFSSL_WRAP_RET(char *, wolfSSL_get_cipher_list, (int a0), (a0), NULL)
WOLFSSL_WRAP_RET(char *, wolfSSL_get_cipher_list_ex, (WOLFSSL * a0, int a1), (a0, a1), NULL)
WOLFSSL_WRAP_RET(const char *, wolfSSL_get_cipher_name, (WOLFSSL * a0), (a0), NULL)
WOLFSSL_WRAP_RET(const char *, wolfSSL_get_cipher_name_from_suite, (unsigned char a0, unsigned char a1), (a0, a1), NULL)
WOLFSSL_WRAP_RET(const char *, wolfSSL_get_cipher_name_iana_from_suite, (unsigned char a0, unsigned char a1), (a0, a1), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_get_cipher_suite_from_name, (const char * a0, unsigned char * a1, unsigned char * a2, int * a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_get_ciphers, (char * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_get_ciphers_iana, (char * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(WOLFSSL_CIPHER *, wolfSSL_get_current_cipher, (WOLFSSL * a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_get_current_cipher_suite, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(const char *, wolfSSL_get_curve_name, (WOLFSSL * a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_get_error, (WOLFSSL * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(void *, wolfSSL_get_ex_data, (const WOLFSSL * a0, int a1), (a0, a1), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_get_fd, (const WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(long int, wolfSSL_get_options, (const WOLFSSL * a0), (a0), ((long int)0))
WOLFSSL_WRAP_RET(WOLFSSL_SESSION *, wolfSSL_get_session, (WOLFSSL * a0), (a0), NULL)
WOLFSSL_WRAP_RET(const char *, wolfSSL_get_shared_ciphers, (WOLFSSL * a0, char * a1, int a2), (a0, a1, a2), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_get_shutdown, (const WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(const char **, wolfSSL_get_system_CA_dirs, (word32 * a0), (a0), NULL)
WOLFSSL_WRAP_RET(long int, wolfSSL_get_verify_depth, (WOLFSSL * a0), (a0), ((long int)0))
WOLFSSL_WRAP_RET(const char *, wolfSSL_get_version, (const WOLFSSL * a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_get_wfd, (const WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_has_pending, (const WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_inject, (WOLFSSL * a0, const void * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_is_init_finished, (const WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_is_server, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_key_update_response, (WOLFSSL * a0, int * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(const char *, wolfSSL_lib_version, (), (), NULL)
WOLFSSL_WRAP_RET(word32, wolfSSL_lib_version_hex, (), (), ((word32)0))
WOLFSSL_WRAP_RET(int, wolfSSL_library_init, (), (), 0)
WOLFSSL_WRAP_VOID(wolfSSL_load_error_strings, (), ())
WOLFSSL_WRAP_RET(int, wolfSSL_make_eap_keys, (WOLFSSL * a0, void * a1, unsigned int a2, const char * a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_mutual_auth, (WOLFSSL * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_negotiate, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(WOLFSSL *, wolfSSL_new, (WOLFSSL_CTX * a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_no_dhe_psk, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_no_ticket_TLSv13, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_only_dhe_psk, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_peek, (WOLFSSL * a0, void * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_pending, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_preferred_group, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_read, (WOLFSSL * a0, void * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_read_ex, (WOLFSSL * a0, void * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_recv, (WOLFSSL * a0, void * a1, int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_require_psk, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_send, (WOLFSSL * a0, const void * a1, int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_session_reused, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_set_cipher_list, (WOLFSSL * a0, const char * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_set_compression, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_set_ex_data, (WOLFSSL * a0, int a1, void * a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_set_fd, (WOLFSSL * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_set_group_messages, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_set_groups, (WOLFSSL * a0, int * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(long int, wolfSSL_set_options, (WOLFSSL * a0, long int a1), (a0, a1), ((long int)0))
WOLFSSL_WRAP_RET(int, wolfSSL_set_read_fd, (WOLFSSL * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_set_session, (WOLFSSL * a0, WOLFSSL_SESSION * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_set_timeout, (WOLFSSL * a0, unsigned int a1), (a0, a1), 0)
WOLFSSL_WRAP_VOID(wolfSSL_set_verify, (WOLFSSL * a0, int a1, VerifyCallback a2), (a0, a1, a2))
WOLFSSL_WRAP_VOID(wolfSSL_set_verify_result, (WOLFSSL * a0, long int a1), (a0, a1))
WOLFSSL_WRAP_RET(int, wolfSSL_set_write_fd, (WOLFSSL * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_shutdown, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_state, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(char *, wolfSSL_strnstr, (const char * a0, const char * a1, size_t a2), (a0, a1, a2), NULL)
WOLFSSL_WRAP_RET(int, wolfSSL_update_keys, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_use_PrivateKey_buffer, (WOLFSSL * a0, const unsigned char * a1, long int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_use_PrivateKey_file, (WOLFSSL * a0, const char * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_use_certificate_ASN1, (WOLFSSL * a0, const unsigned char * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_use_certificate_buffer, (WOLFSSL * a0, const unsigned char * a1, long int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_use_certificate_chain_buffer, (WOLFSSL * a0, const unsigned char * a1, long int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_use_certificate_chain_buffer_format, (WOLFSSL * a0, const unsigned char * a1, long int a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_use_certificate_chain_file, (WOLFSSL * a0, const char * a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_use_certificate_chain_file_format, (WOLFSSL * a0, const char * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_use_certificate_file, (WOLFSSL * a0, const char * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_use_old_poly, (WOLFSSL * a0, int a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_version, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_want_read, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_want_write, (WOLFSSL * a0), (a0), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_write, (WOLFSSL * a0, const void * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_write_ex, (WOLFSSL * a0, const void * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfSSL_writev, (WOLFSSL * a0, const struct iovec * a1, int a2), (a0, a1, a2), 0)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfSSLv23_client_method, (), (), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfSSLv23_client_method_ex, (void * a0), (a0), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfSSLv23_server_method, (), (), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfSSLv23_server_method_ex, (void * a0), (a0), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfTLS_client_method, (), (), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfTLS_client_method_ex, (void * a0), (a0), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfTLS_server_method, (), (), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfTLS_server_method_ex, (void * a0), (a0), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfTLSv1_2_client_method, (), (), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfTLSv1_2_client_method_ex, (void * a0), (a0), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfTLSv1_2_server_method, (), (), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfTLSv1_2_server_method_ex, (void * a0), (a0), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfTLSv1_3_client_method, (), (), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfTLSv1_3_client_method_ex, (void * a0), (a0), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfTLSv1_3_server_method, (), (), NULL)
WOLFSSL_WRAP_RET(WOLFSSL_METHOD *, wolfTLSv1_3_server_method_ex, (void * a0), (a0), NULL)
WOLFSSL_WRAP_RET(int, wolfssl_local_IsValidFQDN, (const char * a0, word32 a1), (a0, a1), 0)
WOLFSSL_WRAP_RET(int, wolfssl_local_MatchBaseName, (int a0, const char * a1, int a2, const char * a3, int a4), (a0, a1, a2, a3, a4), 0)
WOLFSSL_WRAP_RET(int, wolfssl_local_MatchDnsConstraintWildcard, (const char * a0, int a1, const char * a2, int a3, int a4), (a0, a1, a2, a3, a4), 0)
WOLFSSL_WRAP_RET(int, wolfssl_local_MatchDnsNameConstraint, (const char * a0, int a1, const char * a2, int a3, int a4), (a0, a1, a2, a3, a4), 0)
WOLFSSL_WRAP_RET(int, wolfssl_local_MatchIpSubnet, (const byte * a0, int a1, const byte * a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfssl_local_MatchUriNameConstraint, (const char * a0, int a1, const char * a2, int a3), (a0, a1, a2, a3), 0)
WOLFSSL_WRAP_RET(int, wolfssl_local_UriNameHasDnsHost, (const char * a0, int a1), (a0, a1), 0)
/* ---------- 未封装（头文件未声明或不可 dlsym，共 18 个） ---------- */
/* wolfSSL_DupSession (头未声明) */
/* wolfSSL_FreeSession (头未声明) */
/* wolfSSL_GetHmacType_ex (头未声明) */
/* wolfSSL_GetMaxFragSize (头未声明) */
/* wolfSSL_GetSession (头未声明) */
/* wolfSSL_GetSessionClient (头未声明) */
/* wolfSSL_GetSessionFromCache (头未声明) */
/* wolfSSL_NewSession (头未声明) */
/* wolfSSL_ResourceFree (头未声明) */
/* wolfSSL_SetSession (头未声明) */
/* wolfSSL_get_cipher_name_iana (头未声明) */
/* wolfSSL_get_cipher_name_internal (头未声明) */
/* wolfssl_get_global_rng (头未声明) */
/* wolfssl_local_GetMaxPlaintextSize (头未声明) */
/* wolfssl_local_GetRecordSize (头未声明) */
/* wolfssl_local_MaybeCheckAlertOnErr (头未声明) */
/* wolfssl_make_global_rng (头未声明) */
/* wolfssl_make_rng (头未声明) */

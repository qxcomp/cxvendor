/* ============================================================
 * gnutls_dyn.c — GNUTLS 动态加载封装层（公开头文件 API 全量封装）
 *
 * 统计：导出函数 1276 / 封装 1276 / 跳过 0 / 数据符号 38 / 内联类型 245
 * 设计要点（与 rtdb/libxml2_dyn.c 一致）：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不链接目标库（只 -ldl）。
 *   2. 每个封装函数首次调用时才 dlsym 并缓存；未调用的 API 不解析。
 *   3. 编译期内联官方头类型（由 gcc -aux-info 核对签名），无目标库依赖。
 *   4. 加载顺序：显式路径 > GNUTLS_DYN_LIB > LD_LIBRARY_PATH > soname > Nix > 平台默认。
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
typedef enum gnutls_cipher_algorithm {
 GNUTLS_CIPHER_UNKNOWN = 0,
 GNUTLS_CIPHER_NULL = 1,
 GNUTLS_CIPHER_ARCFOUR_128 = 2,
 GNUTLS_CIPHER_3DES_CBC = 3,
 GNUTLS_CIPHER_AES_128_CBC = 4,
 GNUTLS_CIPHER_AES_256_CBC = 5,
 GNUTLS_CIPHER_ARCFOUR_40 = 6,
 GNUTLS_CIPHER_CAMELLIA_128_CBC = 7,
 GNUTLS_CIPHER_CAMELLIA_256_CBC = 8,
 GNUTLS_CIPHER_AES_192_CBC = 9,
 GNUTLS_CIPHER_AES_128_GCM = 10,
 GNUTLS_CIPHER_AES_256_GCM = 11,
 GNUTLS_CIPHER_CAMELLIA_192_CBC = 12,
 GNUTLS_CIPHER_SALSA20_256 = 13,
 GNUTLS_CIPHER_ESTREAM_SALSA20_256 = 14,
 GNUTLS_CIPHER_CAMELLIA_128_GCM = 15,
 GNUTLS_CIPHER_CAMELLIA_256_GCM = 16,
 GNUTLS_CIPHER_RC2_40_CBC = 17,
 GNUTLS_CIPHER_DES_CBC = 18,
 GNUTLS_CIPHER_AES_128_CCM = 19,
 GNUTLS_CIPHER_AES_256_CCM = 20,
 GNUTLS_CIPHER_AES_128_CCM_8 = 21,
 GNUTLS_CIPHER_AES_256_CCM_8 = 22,
 GNUTLS_CIPHER_CHACHA20_POLY1305 = 23,
 GNUTLS_CIPHER_GOST28147_TC26Z_CFB = 24,
 GNUTLS_CIPHER_GOST28147_CPA_CFB = 25,
 GNUTLS_CIPHER_GOST28147_CPB_CFB = 26,
 GNUTLS_CIPHER_GOST28147_CPC_CFB = 27,
 GNUTLS_CIPHER_GOST28147_CPD_CFB = 28,
 GNUTLS_CIPHER_AES_128_CFB8 = 29,
 GNUTLS_CIPHER_AES_192_CFB8 = 30,
 GNUTLS_CIPHER_AES_256_CFB8 = 31,
 GNUTLS_CIPHER_AES_128_XTS = 32,
 GNUTLS_CIPHER_AES_256_XTS = 33,
 GNUTLS_CIPHER_GOST28147_TC26Z_CNT = 34,
 GNUTLS_CIPHER_CHACHA20_64 = 35,
 GNUTLS_CIPHER_CHACHA20_32 = 36,
 GNUTLS_CIPHER_AES_128_SIV = 37,
 GNUTLS_CIPHER_AES_256_SIV = 38,
 GNUTLS_CIPHER_AES_192_GCM = 39,
 GNUTLS_CIPHER_MAGMA_CTR_ACPKM = 40,
 GNUTLS_CIPHER_KUZNYECHIK_CTR_ACPKM = 41,
 GNUTLS_CIPHER_AES_128_SIV_GCM = 42,
 GNUTLS_CIPHER_AES_256_SIV_GCM = 43,
 GNUTLS_CIPHER_AES_128_CFB = 44,
 GNUTLS_CIPHER_AES_192_CFB = 45,
 GNUTLS_CIPHER_AES_256_CFB = 46,
 GNUTLS_CIPHER_IDEA_PGP_CFB = 200,
 GNUTLS_CIPHER_3DES_PGP_CFB = 201,
 GNUTLS_CIPHER_CAST5_PGP_CFB = 202,
 GNUTLS_CIPHER_BLOWFISH_PGP_CFB = 203,
 GNUTLS_CIPHER_SAFER_SK128_PGP_CFB = 204,
 GNUTLS_CIPHER_AES128_PGP_CFB = 205,
 GNUTLS_CIPHER_AES192_PGP_CFB = 206,
 GNUTLS_CIPHER_AES256_PGP_CFB = 207,
 GNUTLS_CIPHER_TWOFISH_PGP_CFB = 208
} gnutls_cipher_algorithm_t;
typedef enum {
 GNUTLS_KX_UNKNOWN = 0,
 GNUTLS_KX_RSA = 1,
 GNUTLS_KX_DHE_DSS = 2,
 GNUTLS_KX_DHE_RSA = 3,
 GNUTLS_KX_ANON_DH = 4,
 GNUTLS_KX_SRP = 5,
 GNUTLS_KX_RSA_EXPORT = 6,
 GNUTLS_KX_SRP_RSA = 7,
 GNUTLS_KX_SRP_DSS = 8,
 GNUTLS_KX_PSK = 9,
 GNUTLS_KX_DHE_PSK = 10,
 GNUTLS_KX_ANON_ECDH = 11,
 GNUTLS_KX_ECDHE_RSA = 12,
 GNUTLS_KX_ECDHE_ECDSA = 13,
 GNUTLS_KX_ECDHE_PSK = 14,
 GNUTLS_KX_RSA_PSK = 15,
 GNUTLS_KX_VKO_GOST_12 = 16
} gnutls_kx_algorithm_t;
typedef enum {
 GNUTLS_PARAMS_RSA_EXPORT = 1,
 GNUTLS_PARAMS_DH = 2,
 GNUTLS_PARAMS_ECDH = 3
} gnutls_params_type_t;
typedef enum {
 GNUTLS_CRD_CERTIFICATE = 1,
 GNUTLS_CRD_ANON,
 GNUTLS_CRD_SRP,
 GNUTLS_CRD_PSK,
 GNUTLS_CRD_IA
} gnutls_credentials_type_t;
typedef enum {
 GNUTLS_MAC_UNKNOWN = 0,
 GNUTLS_MAC_NULL = 1,
 GNUTLS_MAC_MD5 = 2,
 GNUTLS_MAC_SHA1 = 3,
 GNUTLS_MAC_RMD160 = 4,
 GNUTLS_MAC_MD2 = 5,
 GNUTLS_MAC_SHA256 = 6,
 GNUTLS_MAC_SHA384 = 7,
 GNUTLS_MAC_SHA512 = 8,
 GNUTLS_MAC_SHA224 = 9,
 GNUTLS_MAC_SHA3_224 = 10,
 GNUTLS_MAC_SHA3_256 = 11,
 GNUTLS_MAC_SHA3_384 = 12,
 GNUTLS_MAC_SHA3_512 = 13,
 GNUTLS_MAC_MD5_SHA1 = 14,
 GNUTLS_MAC_GOSTR_94 = 15,
 GNUTLS_MAC_STREEBOG_256 = 16,
 GNUTLS_MAC_STREEBOG_512 = 17,
 GNUTLS_MAC_AEAD = 200,
 GNUTLS_MAC_UMAC_96 = 201,
 GNUTLS_MAC_UMAC_128 = 202,
 GNUTLS_MAC_AES_CMAC_128 = 203,
 GNUTLS_MAC_AES_CMAC_256 = 204,
 GNUTLS_MAC_AES_GMAC_128 = 205,
 GNUTLS_MAC_AES_GMAC_192 = 206,
 GNUTLS_MAC_AES_GMAC_256 = 207,
 GNUTLS_MAC_GOST28147_TC26Z_IMIT = 208,
 GNUTLS_MAC_SHAKE_128 = 209,
 GNUTLS_MAC_SHAKE_256 = 210,
 GNUTLS_MAC_MAGMA_OMAC = 211,
 GNUTLS_MAC_KUZNYECHIK_OMAC = 212,
 GNUTLS_MAC_PBMAC1 =
  213
} gnutls_mac_algorithm_t;
typedef enum {
 GNUTLS_DIG_UNKNOWN = GNUTLS_MAC_UNKNOWN,
 GNUTLS_DIG_NULL = GNUTLS_MAC_NULL,
 GNUTLS_DIG_MD5 = GNUTLS_MAC_MD5,
 GNUTLS_DIG_SHA1 = GNUTLS_MAC_SHA1,
 GNUTLS_DIG_RMD160 = GNUTLS_MAC_RMD160,
 GNUTLS_DIG_MD2 = GNUTLS_MAC_MD2,
 GNUTLS_DIG_SHA256 = GNUTLS_MAC_SHA256,
 GNUTLS_DIG_SHA384 = GNUTLS_MAC_SHA384,
 GNUTLS_DIG_SHA512 = GNUTLS_MAC_SHA512,
 GNUTLS_DIG_SHA224 = GNUTLS_MAC_SHA224,
 GNUTLS_DIG_SHA3_224 = GNUTLS_MAC_SHA3_224,
 GNUTLS_DIG_SHA3_256 = GNUTLS_MAC_SHA3_256,
 GNUTLS_DIG_SHA3_384 = GNUTLS_MAC_SHA3_384,
 GNUTLS_DIG_SHA3_512 = GNUTLS_MAC_SHA3_512,
 GNUTLS_DIG_MD5_SHA1 = GNUTLS_MAC_MD5_SHA1,
 GNUTLS_DIG_GOSTR_94 = GNUTLS_MAC_GOSTR_94,
 GNUTLS_DIG_STREEBOG_256 = GNUTLS_MAC_STREEBOG_256,
 GNUTLS_DIG_STREEBOG_512 = GNUTLS_MAC_STREEBOG_512,
 GNUTLS_DIG_SHAKE_128 = GNUTLS_MAC_SHAKE_128,
 GNUTLS_DIG_SHAKE_256 = GNUTLS_MAC_SHAKE_256
} gnutls_digest_algorithm_t;
typedef enum {
 GNUTLS_COMP_UNKNOWN = 0,
 GNUTLS_COMP_NULL = 1,
 GNUTLS_COMP_DEFLATE = 2,
 GNUTLS_COMP_ZLIB = GNUTLS_COMP_DEFLATE,
 GNUTLS_COMP_BROTLI = 3,
 GNUTLS_COMP_ZSTD = 4
} gnutls_compression_method_t;
typedef enum {
 GNUTLS_AL_WARNING = 1,
 GNUTLS_AL_FATAL
} gnutls_alert_level_t;
typedef enum {
 GNUTLS_A_CLOSE_NOTIFY,
 GNUTLS_A_UNEXPECTED_MESSAGE = 10,
 GNUTLS_A_BAD_RECORD_MAC = 20,
 GNUTLS_A_DECRYPTION_FAILED,
 GNUTLS_A_RECORD_OVERFLOW,
 GNUTLS_A_DECOMPRESSION_FAILURE = 30,
 GNUTLS_A_HANDSHAKE_FAILURE = 40,
 GNUTLS_A_SSL3_NO_CERTIFICATE = 41,
 GNUTLS_A_BAD_CERTIFICATE = 42,
 GNUTLS_A_UNSUPPORTED_CERTIFICATE,
 GNUTLS_A_CERTIFICATE_REVOKED,
 GNUTLS_A_CERTIFICATE_EXPIRED,
 GNUTLS_A_CERTIFICATE_UNKNOWN,
 GNUTLS_A_ILLEGAL_PARAMETER,
 GNUTLS_A_UNKNOWN_CA,
 GNUTLS_A_ACCESS_DENIED,
 GNUTLS_A_DECODE_ERROR = 50,
 GNUTLS_A_DECRYPT_ERROR,
 GNUTLS_A_EXPORT_RESTRICTION = 60,
 GNUTLS_A_PROTOCOL_VERSION = 70,
 GNUTLS_A_INSUFFICIENT_SECURITY,
 GNUTLS_A_INTERNAL_ERROR = 80,
 GNUTLS_A_INAPPROPRIATE_FALLBACK = 86,
 GNUTLS_A_USER_CANCELED = 90,
 GNUTLS_A_NO_RENEGOTIATION = 100,
 GNUTLS_A_MISSING_EXTENSION = 109,
 GNUTLS_A_UNSUPPORTED_EXTENSION = 110,
 GNUTLS_A_CERTIFICATE_UNOBTAINABLE = 111,
 GNUTLS_A_UNRECOGNIZED_NAME = 112,
 GNUTLS_A_UNKNOWN_PSK_IDENTITY = 115,
 GNUTLS_A_CERTIFICATE_REQUIRED = 116,
 GNUTLS_A_NO_APPLICATION_PROTOCOL = 120,
 GNUTLS_A_MAX = GNUTLS_A_NO_APPLICATION_PROTOCOL
} gnutls_alert_description_t;
typedef enum {
 GNUTLS_HANDSHAKE_HELLO_REQUEST = 0,
 GNUTLS_HANDSHAKE_CLIENT_HELLO = 1,
 GNUTLS_HANDSHAKE_SERVER_HELLO = 2,
 GNUTLS_HANDSHAKE_HELLO_VERIFY_REQUEST = 3,
 GNUTLS_HANDSHAKE_NEW_SESSION_TICKET = 4,
 GNUTLS_HANDSHAKE_END_OF_EARLY_DATA = 5,
 GNUTLS_HANDSHAKE_ENCRYPTED_EXTENSIONS = 8,
 GNUTLS_HANDSHAKE_CERTIFICATE_PKT = 11,
 GNUTLS_HANDSHAKE_SERVER_KEY_EXCHANGE = 12,
 GNUTLS_HANDSHAKE_CERTIFICATE_REQUEST = 13,
 GNUTLS_HANDSHAKE_SERVER_HELLO_DONE = 14,
 GNUTLS_HANDSHAKE_CERTIFICATE_VERIFY = 15,
 GNUTLS_HANDSHAKE_CLIENT_KEY_EXCHANGE = 16,
 GNUTLS_HANDSHAKE_FINISHED = 20,
 GNUTLS_HANDSHAKE_CERTIFICATE_STATUS = 22,
 GNUTLS_HANDSHAKE_SUPPLEMENTAL = 23,
 GNUTLS_HANDSHAKE_KEY_UPDATE = 24,
 GNUTLS_HANDSHAKE_COMPRESSED_CERTIFICATE_PKT = 25,
 GNUTLS_HANDSHAKE_CHANGE_CIPHER_SPEC = 254,
 GNUTLS_HANDSHAKE_CLIENT_HELLO_V2 = 1024,
 GNUTLS_HANDSHAKE_HELLO_RETRY_REQUEST = 1025,
} gnutls_handshake_description_t;
typedef enum {
 GNUTLS_CERT_IGNORE = 0,
 GNUTLS_CERT_REQUEST = 1,
 GNUTLS_CERT_REQUIRE = 2
} gnutls_certificate_request_t;
typedef enum {
 GNUTLS_OPENPGP_CERT = 0,
 GNUTLS_OPENPGP_CERT_FINGERPRINT = 1
} gnutls_openpgp_crt_status_t;
typedef enum {
 GNUTLS_SHUT_RDWR = 0,
 GNUTLS_SHUT_WR = 1
} gnutls_close_request_t;
typedef enum {
 GNUTLS_SSL3 = 1,
 GNUTLS_TLS1_0 = 2,
 GNUTLS_TLS1 = GNUTLS_TLS1_0,
 GNUTLS_TLS1_1 = 3,
 GNUTLS_TLS1_2 = 4,
 GNUTLS_TLS1_3 = 5,
 GNUTLS_DTLS0_9 = 200,
 GNUTLS_DTLS1_0 = 201,
 GNUTLS_DTLS1_2 = 202,
 GNUTLS_DTLS_VERSION_MIN = GNUTLS_DTLS0_9,
 GNUTLS_DTLS_VERSION_MAX = GNUTLS_DTLS1_2,
 GNUTLS_TLS_VERSION_MAX = GNUTLS_TLS1_3,
 GNUTLS_VERSION_UNKNOWN = 0xff
} gnutls_protocol_t;
typedef enum {
 GNUTLS_CRT_UNKNOWN = 0,
 GNUTLS_CRT_X509 = 1,
 GNUTLS_CRT_OPENPGP = 2,
 GNUTLS_CRT_RAWPK = 3,
 GNUTLS_CRT_MAX = GNUTLS_CRT_RAWPK
} gnutls_certificate_type_t;
typedef enum {
 GNUTLS_X509_FMT_DER = 0,
 GNUTLS_X509_FMT_PEM = 1
} gnutls_x509_crt_fmt_t;
typedef enum gnutls_certificate_print_formats {
 GNUTLS_CRT_PRINT_FULL = 0,
 GNUTLS_CRT_PRINT_ONELINE = 1,
 GNUTLS_CRT_PRINT_UNSIGNED_FULL = 2,
 GNUTLS_CRT_PRINT_COMPACT = 3,
 GNUTLS_CRT_PRINT_FULL_NUMBERS = 4
} gnutls_certificate_print_formats_t;
typedef enum {
 GNUTLS_PK_UNKNOWN = 0,
 GNUTLS_PK_RSA = 1,
 GNUTLS_PK_DSA = 2,
 GNUTLS_PK_DH = 3,
 GNUTLS_PK_ECDSA = 4,
 GNUTLS_PK_ECDH_X25519 = 5,
 GNUTLS_PK_RSA_PSS = 6,
 GNUTLS_PK_EDDSA_ED25519 = 7,
 GNUTLS_PK_GOST_01 = 8,
 GNUTLS_PK_GOST_12_256 = 9,
 GNUTLS_PK_GOST_12_512 = 10,
 GNUTLS_PK_ECDH_X448 = 11,
 GNUTLS_PK_EDDSA_ED448 = 12,
 GNUTLS_PK_RSA_OAEP = 13,
 GNUTLS_PK_MLKEM768 = 14,
 GNUTLS_PK_MLDSA44 = 15,
 GNUTLS_PK_MLDSA65 = 16,
 GNUTLS_PK_MLDSA87 = 17,
 GNUTLS_PK_MLKEM1024 = 18,
 GNUTLS_PK_MAX = GNUTLS_PK_MLKEM1024,
 GNUTLS_PK_EXP_KYBER768 = 256,
 GNUTLS_PK_EXP_MAX = GNUTLS_PK_EXP_KYBER768
} gnutls_pk_algorithm_t;
typedef enum {
 GNUTLS_SIGN_UNKNOWN = 0,
 GNUTLS_SIGN_RSA_SHA1 = 1,
 GNUTLS_SIGN_RSA_SHA = GNUTLS_SIGN_RSA_SHA1,
 GNUTLS_SIGN_DSA_SHA1 = 2,
 GNUTLS_SIGN_DSA_SHA = GNUTLS_SIGN_DSA_SHA1,
 GNUTLS_SIGN_RSA_MD5 = 3,
 GNUTLS_SIGN_RSA_MD2 = 4,
 GNUTLS_SIGN_RSA_RMD160 = 5,
 GNUTLS_SIGN_RSA_SHA256 = 6,
 GNUTLS_SIGN_RSA_SHA384 = 7,
 GNUTLS_SIGN_RSA_SHA512 = 8,
 GNUTLS_SIGN_RSA_SHA224 = 9,
 GNUTLS_SIGN_DSA_SHA224 = 10,
 GNUTLS_SIGN_DSA_SHA256 = 11,
 GNUTLS_SIGN_ECDSA_SHA1 = 12,
 GNUTLS_SIGN_ECDSA_SHA224 = 13,
 GNUTLS_SIGN_ECDSA_SHA256 = 14,
 GNUTLS_SIGN_ECDSA_SHA384 = 15,
 GNUTLS_SIGN_ECDSA_SHA512 = 16,
 GNUTLS_SIGN_DSA_SHA384 = 17,
 GNUTLS_SIGN_DSA_SHA512 = 18,
 GNUTLS_SIGN_ECDSA_SHA3_224 = 20,
 GNUTLS_SIGN_ECDSA_SHA3_256 = 21,
 GNUTLS_SIGN_ECDSA_SHA3_384 = 22,
 GNUTLS_SIGN_ECDSA_SHA3_512 = 23,
 GNUTLS_SIGN_DSA_SHA3_224 = 24,
 GNUTLS_SIGN_DSA_SHA3_256 = 25,
 GNUTLS_SIGN_DSA_SHA3_384 = 26,
 GNUTLS_SIGN_DSA_SHA3_512 = 27,
 GNUTLS_SIGN_RSA_SHA3_224 = 28,
 GNUTLS_SIGN_RSA_SHA3_256 = 29,
 GNUTLS_SIGN_RSA_SHA3_384 = 30,
 GNUTLS_SIGN_RSA_SHA3_512 = 31,
 GNUTLS_SIGN_RSA_PSS_SHA256 = 32,
 GNUTLS_SIGN_RSA_PSS_SHA384 = 33,
 GNUTLS_SIGN_RSA_PSS_SHA512 = 34,
 GNUTLS_SIGN_EDDSA_ED25519 = 35,
 GNUTLS_SIGN_RSA_RAW = 36,
 GNUTLS_SIGN_ECDSA_SECP256R1_SHA256 = 37,
 GNUTLS_SIGN_ECDSA_SECP384R1_SHA384 = 38,
 GNUTLS_SIGN_ECDSA_SECP521R1_SHA512 = 39,
 GNUTLS_SIGN_RSA_PSS_RSAE_SHA256 = 40,
 GNUTLS_SIGN_RSA_PSS_RSAE_SHA384 = 41,
 GNUTLS_SIGN_RSA_PSS_RSAE_SHA512 = 42,
 GNUTLS_SIGN_GOST_94 = 43,
 GNUTLS_SIGN_GOST_256 = 44,
 GNUTLS_SIGN_GOST_512 = 45,
 GNUTLS_SIGN_EDDSA_ED448 = 46,
 GNUTLS_SIGN_MLDSA44 = 47,
 GNUTLS_SIGN_MLDSA65 = 48,
 GNUTLS_SIGN_MLDSA87 = 49,
 GNUTLS_SIGN_MAX = GNUTLS_SIGN_MLDSA87
} gnutls_sign_algorithm_t;
typedef enum {
 GNUTLS_ECC_CURVE_INVALID = 0,
 GNUTLS_ECC_CURVE_SECP224R1,
 GNUTLS_ECC_CURVE_SECP256R1,
 GNUTLS_ECC_CURVE_SECP384R1,
 GNUTLS_ECC_CURVE_SECP521R1,
 GNUTLS_ECC_CURVE_SECP192R1,
 GNUTLS_ECC_CURVE_X25519,
 GNUTLS_ECC_CURVE_ED25519,
 GNUTLS_ECC_CURVE_GOST256CPA,
 GNUTLS_ECC_CURVE_GOST256CPB,
 GNUTLS_ECC_CURVE_GOST256CPC,
 GNUTLS_ECC_CURVE_GOST256CPXA,
 GNUTLS_ECC_CURVE_GOST256CPXB,
 GNUTLS_ECC_CURVE_GOST512A,
 GNUTLS_ECC_CURVE_GOST512B,
 GNUTLS_ECC_CURVE_GOST512C,
 GNUTLS_ECC_CURVE_GOST256A,
 GNUTLS_ECC_CURVE_GOST256B,
 GNUTLS_ECC_CURVE_GOST256C,
 GNUTLS_ECC_CURVE_GOST256D,
 GNUTLS_ECC_CURVE_X448,
 GNUTLS_ECC_CURVE_ED448,
 GNUTLS_ECC_CURVE_MAX = GNUTLS_ECC_CURVE_ED448
} gnutls_ecc_curve_t;
typedef enum {
 GNUTLS_GROUP_INVALID = 0,
 GNUTLS_GROUP_SECP192R1 = GNUTLS_ECC_CURVE_SECP192R1,
 GNUTLS_GROUP_SECP224R1 = GNUTLS_ECC_CURVE_SECP224R1,
 GNUTLS_GROUP_SECP256R1 = GNUTLS_ECC_CURVE_SECP256R1,
 GNUTLS_GROUP_SECP384R1 = GNUTLS_ECC_CURVE_SECP384R1,
 GNUTLS_GROUP_SECP521R1 = GNUTLS_ECC_CURVE_SECP521R1,
 GNUTLS_GROUP_X25519 = GNUTLS_ECC_CURVE_X25519,
 GNUTLS_GROUP_X448 = GNUTLS_ECC_CURVE_X448,
 GNUTLS_GROUP_GC256A = GNUTLS_ECC_CURVE_GOST256A,
 GNUTLS_GROUP_GC256B = GNUTLS_ECC_CURVE_GOST256B,
 GNUTLS_GROUP_GC256C = GNUTLS_ECC_CURVE_GOST256C,
 GNUTLS_GROUP_GC256D = GNUTLS_ECC_CURVE_GOST256D,
 GNUTLS_GROUP_GC512A = GNUTLS_ECC_CURVE_GOST512A,
 GNUTLS_GROUP_GC512B = GNUTLS_ECC_CURVE_GOST512B,
 GNUTLS_GROUP_GC512C = GNUTLS_ECC_CURVE_GOST512C,
 GNUTLS_GROUP_FFDHE2048 = 256,
 GNUTLS_GROUP_FFDHE3072,
 GNUTLS_GROUP_FFDHE4096,
 GNUTLS_GROUP_FFDHE8192,
 GNUTLS_GROUP_FFDHE6144,
 GNUTLS_GROUP_MAX = GNUTLS_GROUP_FFDHE6144,
 GNUTLS_GROUP_EXP_X25519_KYBER768 = 512,
 GNUTLS_GROUP_EXP_SECP256R1_MLKEM768 = 513,
 GNUTLS_GROUP_EXP_SECP384R1_MLKEM1024 = 518,
 GNUTLS_GROUP_EXP_X25519_MLKEM768 = 514,
 GNUTLS_GROUP_EXP_KYBER768 = 515,
 GNUTLS_GROUP_EXP_MLKEM768 = 516,
 GNUTLS_GROUP_EXP_MLKEM1024 = 517,
 GNUTLS_GROUP_EXP_MIN = GNUTLS_GROUP_EXP_X25519_KYBER768,
 GNUTLS_GROUP_EXP_MAX = GNUTLS_GROUP_EXP_SECP384R1_MLKEM1024
} gnutls_group_t;
typedef enum {
 GNUTLS_SEC_PARAM_UNKNOWN = 0,
 GNUTLS_SEC_PARAM_INSECURE = 5,
 GNUTLS_SEC_PARAM_EXPORT = 10,
 GNUTLS_SEC_PARAM_VERY_WEAK = 15,
 GNUTLS_SEC_PARAM_WEAK = 20,
 GNUTLS_SEC_PARAM_LOW = 25,
 GNUTLS_SEC_PARAM_LEGACY = 30,
 GNUTLS_SEC_PARAM_MEDIUM = 35,
 GNUTLS_SEC_PARAM_HIGH = 40,
 GNUTLS_SEC_PARAM_ULTRA = 45,
 GNUTLS_SEC_PARAM_FUTURE = 50,
 GNUTLS_SEC_PARAM_MAX = GNUTLS_SEC_PARAM_FUTURE
} gnutls_sec_param_t;
typedef enum {
 GNUTLS_CB_TLS_UNIQUE,
 GNUTLS_CB_TLS_SERVER_END_POINT,
 GNUTLS_CB_TLS_EXPORTER
} gnutls_channel_binding_t;
typedef enum {
 GNUTLS_GOST_PARAMSET_UNKNOWN = 0,
 GNUTLS_GOST_PARAMSET_TC26_Z,
 GNUTLS_GOST_PARAMSET_CP_A,
 GNUTLS_GOST_PARAMSET_CP_B,
 GNUTLS_GOST_PARAMSET_CP_C,
 GNUTLS_GOST_PARAMSET_CP_D
} gnutls_gost_paramset_t;
typedef enum {
 GNUTLS_CTYPE_CLIENT,
 GNUTLS_CTYPE_SERVER,
 GNUTLS_CTYPE_OURS,
 GNUTLS_CTYPE_PEERS
} gnutls_ctype_target_t;
typedef void *gnutls_transport_ptr_t;
struct gnutls_session_int;
typedef struct gnutls_session_int *gnutls_session_t;
struct gnutls_dh_params_int;
typedef struct gnutls_dh_params_int *gnutls_dh_params_t;
struct gnutls_x509_privkey_int;
typedef struct gnutls_x509_privkey_int *gnutls_rsa_params_t;
struct gnutls_priority_st;
typedef struct gnutls_priority_st *gnutls_priority_t;
typedef struct {
 unsigned char *data;
 unsigned int size;
} gnutls_datum_t;
typedef struct gnutls_library_config_st {
 const char *name;
 const char *value;
} gnutls_library_config_st;
typedef struct gnutls_params_st {
 gnutls_params_type_t type;
 union params {
  gnutls_dh_params_t dh;
  gnutls_rsa_params_t rsa_export;
 } params;
 int deinit;
} gnutls_params_st;
typedef int gnutls_params_function(gnutls_session_t, gnutls_params_type_t,
       gnutls_params_st *);
typedef struct {
 size_t low;
 size_t high;
} gnutls_range_st;
typedef struct mbuffer_st *gnutls_packet_t;
typedef enum {
 GNUTLS_NAME_DNS = 1
} gnutls_server_name_type_t;
typedef enum {
 GNUTLS_SUPPLEMENTAL_UNKNOWN = 0,
} gnutls_supplemental_data_format_type_t;
typedef enum {
 GNUTLS_SRTP_AES128_CM_HMAC_SHA1_80 = 0x0001,
 GNUTLS_SRTP_AES128_CM_HMAC_SHA1_32 = 0x0002,
 GNUTLS_SRTP_NULL_HMAC_SHA1_80 = 0x0005,
 GNUTLS_SRTP_NULL_HMAC_SHA1_32 = 0x0006,
 GNUTLS_SRTP_AEAD_AES_128_GCM = 0x0007,
 GNUTLS_SRTP_AEAD_AES_256_GCM = 0x0008
} gnutls_srtp_profile_t;
typedef int gnutls_certificate_verify_function(gnutls_session_t);
typedef enum {
 GNUTLS_DT_UNKNOWN = 0,
 GNUTLS_DT_DNS_HOSTNAME = 1,
 GNUTLS_DT_KEY_PURPOSE_OID = 2,
 GNUTLS_DT_RFC822NAME = 3,
 GNUTLS_DT_IP_ADDRESS = 4
} gnutls_vdata_types_t;
typedef struct {
 gnutls_vdata_types_t type;
 unsigned char *data;
 unsigned int size;
} gnutls_typed_vdata_st;
typedef int (*gnutls_db_store_func)(void *, gnutls_datum_t key,
        gnutls_datum_t data);
typedef int (*gnutls_db_remove_func)(void *, gnutls_datum_t key);
typedef gnutls_datum_t (*gnutls_db_retr_func)(void *, gnutls_datum_t key);
typedef int (*gnutls_handshake_hook_func)(gnutls_session_t, unsigned int htype,
       unsigned when, unsigned int incoming,
       const gnutls_datum_t *msg);
typedef int (*gnutls_handshake_simple_hook_func)(gnutls_session_t);
struct gnutls_pubkey_st;
typedef struct gnutls_pubkey_st *gnutls_pubkey_t;
struct gnutls_privkey_st;
typedef struct gnutls_privkey_st *gnutls_privkey_t;
typedef struct gnutls_x509_privkey_int *gnutls_x509_privkey_t;
struct gnutls_x509_crl_int;
typedef struct gnutls_x509_crl_int *gnutls_x509_crl_t;
struct gnutls_x509_crt_int;
typedef struct gnutls_x509_crt_int *gnutls_x509_crt_t;
struct gnutls_x509_crq_int;
typedef struct gnutls_x509_crq_int *gnutls_x509_crq_t;
struct gnutls_openpgp_keyring_int;
typedef struct gnutls_openpgp_keyring_int *gnutls_openpgp_keyring_t;
struct gnutls_certificate_credentials_st;
typedef struct gnutls_certificate_credentials_st
 *gnutls_certificate_credentials_t;
typedef struct gnutls_anon_server_credentials_st
 *gnutls_anon_server_credentials_t;
typedef struct gnutls_anon_client_credentials_st
 *gnutls_anon_client_credentials_t;
typedef int (*gnutls_status_request_ocsp_func)(gnutls_session_t session,
            void *ptr,
            gnutls_datum_t *ocsp_response);
typedef struct gnutls_ocsp_data_st {
 unsigned int version;
 gnutls_datum_t response;
 time_t exptime;
 unsigned char padding[32];
} gnutls_ocsp_data_st;
typedef time_t (*gnutls_time_func)(time_t *t);
typedef int (*mutex_init_func)(void **mutex);
typedef int (*mutex_lock_func)(void **mutex);
typedef int (*mutex_unlock_func)(void **mutex);
typedef int (*mutex_deinit_func)(void **mutex);
typedef void *(*gnutls_alloc_function)(size_t);
typedef void *(*gnutls_calloc_function)(size_t, size_t);
typedef int (*gnutls_is_secure_function)(const void *);
typedef void (*gnutls_free_function)(void *);
typedef void *(*gnutls_realloc_function)(void *, size_t);
typedef void (*gnutls_log_func)(int, const char *);
typedef void (*gnutls_audit_log_func)(gnutls_session_t, const char *);
typedef int (*gnutls_keylog_func)(gnutls_session_t session, const char *label,
      const gnutls_datum_t *secret);
struct iovec
  {
    void *iov_base;
    size_t iov_len;
  };
typedef struct iovec giovec_t;
typedef ssize_t (*gnutls_pull_func)(gnutls_transport_ptr_t, void *,
         size_t);
typedef ssize_t (*gnutls_push_func)(gnutls_transport_ptr_t, const void *,
        size_t);
typedef int (*gnutls_pull_timeout_func)(gnutls_transport_ptr_t,
     unsigned int ms);
typedef ssize_t (*gnutls_vec_push_func)(gnutls_transport_ptr_t,
     const giovec_t *iov, int iovcnt);
typedef int (*gnutls_errno_func)(gnutls_transport_ptr_t);
typedef enum gnutls_random_art {
 GNUTLS_RANDOM_ART_OPENSSH = 1
} gnutls_random_art_t;
typedef struct gnutls_srp_server_credentials_st *gnutls_srp_server_credentials_t;
typedef struct gnutls_srp_client_credentials_st *gnutls_srp_client_credentials_t;
typedef int gnutls_srp_server_credentials_function(gnutls_session_t,
         const char *username,
         gnutls_datum_t *salt,
         gnutls_datum_t *verifier,
         gnutls_datum_t *generator,
         gnutls_datum_t *prime);
typedef int gnutls_srp_client_credentials_function(gnutls_session_t, char **,
         char **);
typedef struct gnutls_psk_server_credentials_st *gnutls_psk_server_credentials_t;
typedef struct gnutls_psk_client_credentials_st *gnutls_psk_client_credentials_t;
typedef enum gnutls_psk_key_flags {
 GNUTLS_PSK_KEY_RAW = 0,
 GNUTLS_PSK_KEY_HEX = 1 << 0,
 GNUTLS_PSK_KEY_EXT = 1 << 1
} gnutls_psk_key_flags;
typedef int gnutls_psk_server_credentials_function(gnutls_session_t,
         const char *username,
         gnutls_datum_t *key);
typedef int gnutls_psk_server_credentials_function2(
 gnutls_session_t, const gnutls_datum_t *username, gnutls_datum_t *key);
typedef int gnutls_psk_server_credentials_function3(
 gnutls_session_t, const gnutls_datum_t *username, gnutls_datum_t *key,
 gnutls_psk_key_flags *flags);
typedef int gnutls_psk_client_credentials_function(gnutls_session_t,
         char **username,
         gnutls_datum_t *key);
typedef int gnutls_psk_client_credentials_function2(gnutls_session_t,
          gnutls_datum_t *username,
          gnutls_datum_t *key);
typedef int gnutls_psk_client_credentials_function3(
 gnutls_session_t, gnutls_datum_t *username, gnutls_datum_t *key,
 gnutls_psk_key_flags *flags);
typedef enum gnutls_x509_subject_alt_name_t {
 GNUTLS_SAN_DNSNAME = 1,
 GNUTLS_SAN_RFC822NAME = 2,
 GNUTLS_SAN_URI = 3,
 GNUTLS_SAN_IPADDRESS = 4,
 GNUTLS_SAN_OTHERNAME = 5,
 GNUTLS_SAN_DN = 6,
 GNUTLS_SAN_REGISTERED_ID = 7,
 GNUTLS_SAN_MIN = GNUTLS_SAN_DNSNAME,
 GNUTLS_SAN_MAX = GNUTLS_SAN_REGISTERED_ID,
 GNUTLS_SAN_OTHERNAME_XMPP = 1000,
 GNUTLS_SAN_OTHERNAME_KRB5PRINCIPAL,
 GNUTLS_SAN_OTHERNAME_MSUSERPRINCIPAL,
 GNUTLS_SAN_OTHERNAME_SRV,
 GNUTLS_SAN_OTHERNAME_MIN = GNUTLS_SAN_OTHERNAME_XMPP,
 GNUTLS_SAN_OTHERNAME_MAX = GNUTLS_SAN_OTHERNAME_SRV
} gnutls_x509_subject_alt_name_t;
struct gnutls_openpgp_crt_int;
typedef struct gnutls_openpgp_crt_int *gnutls_openpgp_crt_t;
struct gnutls_openpgp_privkey_int;
typedef struct gnutls_openpgp_privkey_int *gnutls_openpgp_privkey_t;
struct gnutls_pkcs11_privkey_st;
typedef struct gnutls_pkcs11_privkey_st *gnutls_pkcs11_privkey_t;
typedef enum {
 GNUTLS_PRIVKEY_X509,
 GNUTLS_PRIVKEY_OPENPGP,
 GNUTLS_PRIVKEY_PKCS11,
 GNUTLS_PRIVKEY_EXT
} gnutls_privkey_type_t;
typedef struct gnutls_retr2_st {
 gnutls_certificate_type_t cert_type;
 gnutls_privkey_type_t key_type;
 union {
  gnutls_x509_crt_t *x509;
  gnutls_openpgp_crt_t pgp;
 } cert;
 unsigned int ncerts;
 union {
  gnutls_x509_privkey_t x509;
  gnutls_openpgp_privkey_t pgp;
  gnutls_pkcs11_privkey_t pkcs11;
 } key;
 unsigned int deinit_all;
} gnutls_retr2_st;
typedef int gnutls_certificate_retrieve_function(
 gnutls_session_t, const gnutls_datum_t *req_ca_rdn, int nreqs,
 const gnutls_pk_algorithm_t *pk_algos, int pk_algos_length,
 gnutls_retr2_st *);
typedef int (*gnutls_tdb_store_func)(const char *db_name, const char *host,
         const char *service, time_t expiration,
         const gnutls_datum_t *pubkey);
typedef int (*gnutls_tdb_store_commitment_func)(
 const char *db_name, const char *host, const char *service,
 time_t expiration, gnutls_digest_algorithm_t hash_algo,
 const gnutls_datum_t *hash);
typedef int (*gnutls_tdb_verify_func)(const char *db_name, const char *host,
          const char *service,
          const gnutls_datum_t *pubkey);
struct gnutls_tdb_int;
typedef struct gnutls_tdb_int *gnutls_tdb_t;
typedef int (*gnutls_pin_callback_t)(void *userdata, int attempt,
         const char *token_url,
         const char *token_label,
         unsigned int flags, char *pin,
         size_t pin_max);
typedef struct gnutls_buffer_st *gnutls_buffer_t;
typedef void *gnutls_ext_priv_data_t;
typedef int (*gnutls_ext_recv_func)(gnutls_session_t session,
        const unsigned char *data, size_t len);
typedef int (*gnutls_ext_send_func)(gnutls_session_t session,
        gnutls_buffer_t extdata);
typedef void (*gnutls_ext_deinit_data_func)(gnutls_ext_priv_data_t data);
typedef int (*gnutls_ext_pack_func)(gnutls_ext_priv_data_t data,
        gnutls_buffer_t packed_data);
typedef int (*gnutls_ext_unpack_func)(gnutls_buffer_t packed_data,
          gnutls_ext_priv_data_t *data);
typedef int (*gnutls_ext_raw_process_func)(void *ctx, unsigned tls_id,
        const unsigned char *data,
        unsigned data_size);
typedef enum {
 GNUTLS_EXT_ANY = 0,
 GNUTLS_EXT_APPLICATION = 1,
 GNUTLS_EXT_TLS = 2,
 GNUTLS_EXT_MANDATORY = 3,
 GNUTLS_EXT_NONE = 4,
 GNUTLS_EXT_VERSION_NEG = 5
} gnutls_ext_parse_type_t;
typedef int (*gnutls_supp_recv_func)(gnutls_session_t session,
         const unsigned char *data,
         size_t data_size);
typedef int (*gnutls_supp_send_func)(gnutls_session_t session,
         gnutls_buffer_t buf);
typedef struct gnutls_anti_replay_st *gnutls_anti_replay_t;
typedef int (*gnutls_db_add_func)(void *, time_t exp_time,
      const gnutls_datum_t *key,
      const gnutls_datum_t *data);
typedef enum {
 GNUTLS_ENCRYPTION_LEVEL_INITIAL,
 GNUTLS_ENCRYPTION_LEVEL_EARLY,
 GNUTLS_ENCRYPTION_LEVEL_HANDSHAKE,
 GNUTLS_ENCRYPTION_LEVEL_APPLICATION
} gnutls_record_encryption_level_t;
typedef int (*gnutls_handshake_read_func)(gnutls_session_t session,
       gnutls_record_encryption_level_t level,
       gnutls_handshake_description_t htype,
       const void *data, size_t data_size);
typedef int (*gnutls_handshake_secret_func)(
 gnutls_session_t session, gnutls_record_encryption_level_t level,
 const void *secret_read, const void *secret_write, size_t secret_size);
typedef int (*gnutls_alert_read_func)(gnutls_session_t session,
          gnutls_record_encryption_level_t level,
          gnutls_alert_level_t alert_level,
          gnutls_alert_description_t alert_desc);
typedef enum gnutls_fips_mode_t {
 GNUTLS_FIPS140_DISABLED = 0,
 GNUTLS_FIPS140_STRICT = 1,
 GNUTLS_FIPS140_SELFTESTS = 2,
 GNUTLS_FIPS140_LAX = 3,
 GNUTLS_FIPS140_LOG = 4
} gnutls_fips_mode_t;
typedef struct gnutls_fips140_context_st *gnutls_fips140_context_t;
typedef enum {
 GNUTLS_FIPS140_OP_INITIAL,
 GNUTLS_FIPS140_OP_APPROVED,
 GNUTLS_FIPS140_OP_NOT_APPROVED,
 GNUTLS_FIPS140_OP_ERROR
} gnutls_fips140_operation_state_t;
typedef enum {
 GNUTLS_KTLS_RECV = 1 << 0,
 GNUTLS_KTLS_SEND = 1 << 1,
 GNUTLS_KTLS_DUPLEX = GNUTLS_KTLS_RECV | GNUTLS_KTLS_SEND,
} gnutls_transport_ktls_enable_flags_t;
typedef gnutls_cipher_algorithm_t gnutls_cipher_algorithm
 __attribute__((__deprecated__));
typedef struct gnutls_name_constraints_st *gnutls_x509_name_constraints_t;
typedef struct gnutls_x509_spki_st *gnutls_x509_spki_t;
typedef struct gnutls_x509_tlsfeatures_st *gnutls_x509_tlsfeatures_t;
typedef enum gnutls_x509_qualifier_t {
 GNUTLS_X509_QUALIFIER_UNKNOWN = 0,
 GNUTLS_X509_QUALIFIER_URI,
 GNUTLS_X509_QUALIFIER_NOTICE
} gnutls_x509_qualifier_t;
typedef struct gnutls_x509_policy_st {
 char *oid;
 unsigned int qualifiers;
 struct {
  gnutls_x509_qualifier_t type;
  char *data;
  unsigned int size;
 } qualifier[8];
} gnutls_x509_policy_st;
typedef struct gnutls_x509_dn_st *gnutls_x509_dn_t;
typedef struct gnutls_x509_ava_st {
 gnutls_datum_t oid;
 gnutls_datum_t value;
 unsigned long value_tag;
} gnutls_x509_ava_st;
typedef struct gnutls_x509_crl_iter *gnutls_x509_crl_iter_t;
typedef enum gnutls_certificate_verification_profiles_t {
 GNUTLS_PROFILE_UNKNOWN = 0,
 GNUTLS_PROFILE_VERY_WEAK = 1,
 GNUTLS_PROFILE_LOW = 2,
 GNUTLS_PROFILE_LEGACY = 4,
 GNUTLS_PROFILE_MEDIUM = 5,
 GNUTLS_PROFILE_HIGH = 6,
 GNUTLS_PROFILE_ULTRA = 7,
 GNUTLS_PROFILE_FUTURE = 8,
 GNUTLS_PROFILE_SUITEB128 = 32,
 GNUTLS_PROFILE_SUITEB192 = 33
} gnutls_certificate_verification_profiles_t;
typedef enum {
 GNUTLS_KEYGEN_SEED = 1,
 GNUTLS_KEYGEN_DIGEST = 2,
 GNUTLS_KEYGEN_SPKI = 3,
 GNUTLS_KEYGEN_DH = 4
} gnutls_keygen_types_t;
typedef struct {
 gnutls_keygen_types_t type;
 unsigned char *data;
 unsigned int size;
} gnutls_keygen_data_st;
typedef struct gnutls_x509_trust_list_st *gnutls_x509_trust_list_t;
typedef struct gnutls_x509_trust_list_iter *gnutls_x509_trust_list_iter_t;
typedef int gnutls_verify_output_function(
 gnutls_x509_crt_t cert, gnutls_x509_crt_t issuer,
 gnutls_x509_crl_t crl,
 unsigned int verification_output);
typedef int gnutls_x509_trust_list_getissuer_function(
 gnutls_x509_trust_list_t list, const gnutls_x509_crt_t cert,
 gnutls_x509_crt_t **issuers, unsigned int *issuers_size);
typedef struct gnutls_x509_ext_st {
 char *oid;
 unsigned int critical;
 gnutls_datum_t data;
} gnutls_x509_ext_st;
struct gnutls_pkcs7_int;
typedef struct gnutls_pkcs7_int *gnutls_pkcs7_t;
typedef struct gnutls_pkcs7_attrs_st *gnutls_pkcs7_attrs_t;
typedef struct gnutls_pkcs7_signature_info_st {
 gnutls_sign_algorithm_t algo;
 gnutls_datum_t sig;
 gnutls_datum_t issuer_dn;
 gnutls_datum_t signer_serial;
 gnutls_datum_t issuer_keyid;
 time_t signing_time;
 gnutls_pkcs7_attrs_t signed_attrs;
 gnutls_pkcs7_attrs_t unsigned_attrs;
 char pad[64];
} gnutls_pkcs7_signature_info_st;
typedef int (*gnutls_pkcs11_token_callback_t)(void *const userdata,
           const char *const label,
           unsigned retry);
struct gnutls_pkcs11_obj_st;
typedef struct gnutls_pkcs11_obj_st *gnutls_pkcs11_obj_t;
typedef enum {
 GNUTLS_PKCS11_URL_GENERIC,
 GNUTLS_PKCS11_URL_LIB,
 GNUTLS_PKCS11_URL_LIB_VERSION
} gnutls_pkcs11_url_type_t;
typedef enum {
 GNUTLS_PKCS11_OBJ_ID_HEX = 1,
 GNUTLS_PKCS11_OBJ_LABEL,
 GNUTLS_PKCS11_OBJ_TOKEN_LABEL,
 GNUTLS_PKCS11_OBJ_TOKEN_SERIAL,
 GNUTLS_PKCS11_OBJ_TOKEN_MANUFACTURER,
 GNUTLS_PKCS11_OBJ_TOKEN_MODEL,
 GNUTLS_PKCS11_OBJ_ID,
 GNUTLS_PKCS11_OBJ_LIBRARY_VERSION,
 GNUTLS_PKCS11_OBJ_LIBRARY_DESCRIPTION,
 GNUTLS_PKCS11_OBJ_LIBRARY_MANUFACTURER
} gnutls_pkcs11_obj_info_t;
typedef enum {
 GNUTLS_PKCS11_TOKEN_LABEL,
 GNUTLS_PKCS11_TOKEN_SERIAL,
 GNUTLS_PKCS11_TOKEN_MANUFACTURER,
 GNUTLS_PKCS11_TOKEN_MODEL,
 GNUTLS_PKCS11_TOKEN_MODNAME
} gnutls_pkcs11_token_info_t;
typedef enum {
 GNUTLS_PKCS11_OBJ_UNKNOWN,
 GNUTLS_PKCS11_OBJ_X509_CRT,
 GNUTLS_PKCS11_OBJ_PUBKEY,
 GNUTLS_PKCS11_OBJ_PRIVKEY,
 GNUTLS_PKCS11_OBJ_SECRET_KEY,
 GNUTLS_PKCS11_OBJ_DATA,
 GNUTLS_PKCS11_OBJ_X509_CRT_EXTENSION
} gnutls_pkcs11_obj_type_t;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic pop
typedef enum gnutls_openpgp_crt_fmt {
 GNUTLS_OPENPGP_FMT_RAW,
 GNUTLS_OPENPGP_FMT_BASE64
} gnutls_openpgp_crt_fmt_t;
typedef int (*gnutls_openpgp_recv_key_func)(gnutls_session_t session,
         const unsigned char *keyfpr,
         unsigned int keyfpr_length,
         gnutls_datum_t *key);
struct tpm_key_list_st;
typedef struct tpm_key_list_st *gnutls_tpm_key_list_t;
typedef enum {
 GNUTLS_TPMKEY_FMT_RAW = 0,
 GNUTLS_TPMKEY_FMT_DER = GNUTLS_TPMKEY_FMT_RAW,
 GNUTLS_TPMKEY_FMT_CTK_PEM = 1
} gnutls_tpmkey_fmt_t;
typedef int (*gnutls_privkey_sign_func)(gnutls_privkey_t key, void *userdata,
     const gnutls_datum_t *raw_data,
     gnutls_datum_t *signature);
typedef int (*gnutls_privkey_decrypt_func)(gnutls_privkey_t key, void *userdata,
        const gnutls_datum_t *ciphertext,
        gnutls_datum_t *plaintext);
typedef int (*gnutls_privkey_sign_hash_func)(gnutls_privkey_t key,
          gnutls_sign_algorithm_t algo,
          void *userdata, unsigned int flags,
          const gnutls_datum_t *hash,
          gnutls_datum_t *signature);
typedef int (*gnutls_privkey_sign_data_func)(gnutls_privkey_t key,
          gnutls_sign_algorithm_t algo,
          void *userdata, unsigned int flags,
          const gnutls_datum_t *data,
          gnutls_datum_t *signature);
typedef void (*gnutls_privkey_deinit_func)(gnutls_privkey_t key,
        void *userdata);
typedef int (*gnutls_privkey_info_func)(gnutls_privkey_t key,
     unsigned int flags, void *userdata);
typedef struct gnutls_pcert_st {
 gnutls_pubkey_t pubkey;
 gnutls_datum_t cert;
 gnutls_certificate_type_t type;
} gnutls_pcert_st;
typedef int gnutls_certificate_retrieve_function2(
 gnutls_session_t, const gnutls_datum_t *req_ca_rdn, int nreqs,
 const gnutls_pk_algorithm_t *pk_algos, int pk_algos_length,
 gnutls_pcert_st **, unsigned int *pcert_length,
 gnutls_privkey_t *privkey);
struct gnutls_cert_retr_st {
 unsigned version;
 gnutls_certificate_credentials_t cred;
 const gnutls_datum_t *req_ca_rdn;
 unsigned nreqs;
 const gnutls_pk_algorithm_t *pk_algos;
 unsigned pk_algos_length;
 unsigned char padding[64];
};
typedef int gnutls_certificate_retrieve_function3(
 gnutls_session_t, const struct gnutls_cert_retr_st *info,
 gnutls_pcert_st **certs, unsigned int *certs_length,
 gnutls_ocsp_data_st **ocsp, unsigned int *ocsp_length,
 gnutls_privkey_t *privkey, unsigned int *flags);
typedef struct api_cipher_hd_st *gnutls_cipher_hd_t;
typedef struct api_aead_cipher_hd_st *gnutls_aead_cipher_hd_t;
typedef struct hash_hd_st *gnutls_hash_hd_t;
typedef struct hmac_hd_st *gnutls_hmac_hd_t;
typedef enum gnutls_rnd_level {
 GNUTLS_RND_NONCE = 0,
 GNUTLS_RND_RANDOM = 1,
 GNUTLS_RND_KEY = 2
} gnutls_rnd_level_t;
typedef int (*gnutls_cipher_init_func)(gnutls_cipher_algorithm_t, void **ctx,
           int enc);
typedef int (*gnutls_cipher_setkey_func)(void *ctx, const void *key,
      size_t keysize);
typedef int (*gnutls_cipher_setiv_func)(void *ctx, const void *iv,
     size_t ivsize);
typedef int (*gnutls_cipher_encrypt_func)(void *ctx, const void *plain,
       size_t plainsize, void *encr,
       size_t encrsize);
typedef int (*gnutls_cipher_decrypt_func)(void *ctx, const void *encr,
       size_t encrsize, void *plain,
       size_t plainsize);
typedef int (*gnutls_cipher_aead_encrypt_func)(
 void *ctx, const void *nonce, size_t noncesize, const void *auth,
 size_t authsize, size_t tag_size, const void *plain, size_t plainsize,
 void *encr, size_t encrsize);
typedef int (*gnutls_cipher_aead_decrypt_func)(
 void *ctx, const void *nonce, size_t noncesize, const void *auth,
 size_t authsize, size_t tag_size, const void *encr, size_t encrsize,
 void *plain, size_t plainsize);
typedef void (*gnutls_cipher_deinit_func)(void *ctx);
typedef int (*gnutls_mac_init_func)(gnutls_mac_algorithm_t, void **ctx);
typedef int (*gnutls_mac_setkey_func)(void *ctx, const void *key,
          size_t keysize);
typedef int (*gnutls_mac_setnonce_func)(void *ctx, const void *nonce,
     size_t noncesize);
typedef int (*gnutls_mac_hash_func)(void *ctx, const void *text,
        size_t textsize);
typedef int (*gnutls_mac_output_func)(void *src_ctx, void *digest,
          size_t digestsize);
typedef void (*gnutls_mac_deinit_func)(void *ctx);
typedef int (*gnutls_mac_fast_func)(gnutls_mac_algorithm_t, const void *nonce,
        size_t nonce_size, const void *key,
        size_t keysize, const void *text,
        size_t textsize, void *digest);
typedef int (*gnutls_digest_init_func)(gnutls_digest_algorithm_t, void **ctx);
typedef int (*gnutls_digest_hash_func)(void *ctx, const void *text,
           size_t textsize);
typedef int (*gnutls_digest_output_func)(void *src_ctx, void *digest,
      size_t digestsize);
typedef void (*gnutls_digest_deinit_func)(void *ctx);
typedef int (*gnutls_digest_fast_func)(gnutls_digest_algorithm_t,
           const void *text, size_t textsize,
           void *digest);
typedef struct {
 unsigned int record_seq;
 unsigned int hsk_read_seq;
 unsigned int hsk_write_seq;
} gnutls_dtls_prestate_st;
#pragma GCC diagnostic pop
typedef enum gnutls_hpke_kem_t {
 GNUTLS_HPKE_KEM_DHKEM_P256 = 0x0010,
 GNUTLS_HPKE_KEM_DHKEM_P384 = 0x0011,
 GNUTLS_HPKE_KEM_DHKEM_P521 = 0x0012,
 GNUTLS_HPKE_KEM_DHKEM_X25519 = 0x0020,
 GNUTLS_HPKE_KEM_DHKEM_X448 = 0x0021
} gnutls_hpke_kem_t;
typedef enum gnutls_hpke_kdf_t {
 GNUTLS_HPKE_KDF_HKDF_SHA256 = 0x0001,
 GNUTLS_HPKE_KDF_HKDF_SHA384 = 0x0002,
 GNUTLS_HPKE_KDF_HKDF_SHA512 = 0x0003
} gnutls_hpke_kdf_t;
typedef enum gnutls_hpke_aead_t {
 GNUTLS_HPKE_AEAD_AES_128_GCM = 0x0001,
 GNUTLS_HPKE_AEAD_AES_256_GCM = 0x0002,
 GNUTLS_HPKE_AEAD_CHACHA20_POLY1305 = 0x0003,
 GNUTLS_HPKE_AEAD_EXPORT_ONLY = 0xFFFF
} gnutls_hpke_aead_t;
typedef enum gnutls_hpke_mode_t {
 GNUTLS_HPKE_MODE_BASE = 0,
 GNUTLS_HPKE_MODE_PSK = 1,
 GNUTLS_HPKE_MODE_AUTH = 2,
 GNUTLS_HPKE_MODE_AUTH_PSK = 3
} gnutls_hpke_mode_t;
typedef enum gnutls_hpke_role_t {
 GNUTLS_HPKE_ROLE_SENDER = 0,
 GNUTLS_HPKE_ROLE_RECEIVER = 1
} gnutls_hpke_role_t;
typedef struct gnutls_hpke_context_st *gnutls_hpke_context_t;
typedef enum gnutls_ocsp_print_formats_t {
 GNUTLS_OCSP_PRINT_FULL = 0,
 GNUTLS_OCSP_PRINT_COMPACT = 1
} gnutls_ocsp_print_formats_t;
struct gnutls_ocsp_req_int;
typedef struct gnutls_ocsp_req_int *gnutls_ocsp_req_t;
typedef const struct gnutls_ocsp_req_int *gnutls_ocsp_req_const_t;
struct gnutls_ocsp_resp_int;
typedef struct gnutls_ocsp_resp_int *gnutls_ocsp_resp_t;
typedef const struct gnutls_ocsp_resp_int *gnutls_ocsp_resp_const_t;
struct gnutls_pkcs12_int;
typedef struct gnutls_pkcs12_int *gnutls_pkcs12_t;
struct gnutls_pkcs12_bag_int;
typedef struct gnutls_pkcs12_bag_int *gnutls_pkcs12_bag_t;
typedef enum gnutls_pkcs12_bag_type_t {
 GNUTLS_BAG_EMPTY = 0,
 GNUTLS_BAG_PKCS8_ENCRYPTED_KEY = 1,
 GNUTLS_BAG_PKCS8_KEY = 2,
 GNUTLS_BAG_CERTIFICATE = 3,
 GNUTLS_BAG_CRL = 4,
 GNUTLS_BAG_SECRET = 5,
 GNUTLS_BAG_ENCRYPTED = 10,
 GNUTLS_BAG_UNKNOWN = 20
} gnutls_pkcs12_bag_type_t;
typedef __socklen_t socklen_t;
typedef unsigned short int sa_family_t;
struct __attribute__ ((__may_alias__)) sockaddr
  {
    sa_family_t sa_family;
    char sa_data[14];
  };
struct system_key_iter_st;
typedef struct system_key_iter_st *gnutls_system_key_iter_t;
typedef int (*gnutls_privkey_import_url_func)(gnutls_privkey_t pkey,
           const char *url, unsigned flags);
typedef int (*gnutls_x509_crt_import_url_func)(gnutls_x509_crt_t pkey,
            const char *url, unsigned flags);
typedef int (*gnutls_pubkey_import_url_func)(gnutls_pubkey_t pkey,
          const char *url, unsigned flags);
typedef int (*gnutls_get_raw_issuer_func)(const char *url,
       gnutls_x509_crt_t crt,
       gnutls_datum_t *issuer_der,
       unsigned flags);
typedef struct gnutls_custom_url_st {
 const char *name;
 unsigned name_size;
 gnutls_privkey_import_url_func import_key;
 gnutls_x509_crt_import_url_func import_crt;
 gnutls_pubkey_import_url_func import_pubkey;
 gnutls_get_raw_issuer_func get_issuer;
 void *future1;
 void *future2;
} gnutls_custom_url_st;
typedef struct gnutls_subject_alt_names_st *gnutls_subject_alt_names_t;
typedef struct gnutls_x509_crl_dist_points_st *gnutls_x509_crl_dist_points_t;
typedef struct gnutls_x509_aia_st *gnutls_x509_aia_t;
typedef struct gnutls_x509_aki_st *gnutls_x509_aki_t;
typedef struct gnutls_x509_key_purposes_st *gnutls_x509_key_purposes_t;
typedef struct gnutls_x509_policies_st *gnutls_x509_policies_t;
typedef struct gnutls_x509_ct_scts_st *gnutls_x509_ct_scts_t;

/* ============================================================
 * 平台适配层：统一动态库 API 与默认库名
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
/* ---------- 数据符号副本 ---------- */
gnutls_calloc_function gnutls_calloc = {0};
gnutls_datum_t gnutls_ffdhe_2048_group_generator = {0};
gnutls_datum_t gnutls_ffdhe_2048_group_prime = {0};
gnutls_datum_t gnutls_ffdhe_2048_group_q = {0};
unsigned int gnutls_ffdhe_2048_key_bits = {0};
gnutls_datum_t gnutls_ffdhe_3072_group_generator = {0};
gnutls_datum_t gnutls_ffdhe_3072_group_prime = {0};
gnutls_datum_t gnutls_ffdhe_3072_group_q = {0};
unsigned int gnutls_ffdhe_3072_key_bits = {0};
gnutls_datum_t gnutls_ffdhe_4096_group_generator = {0};
gnutls_datum_t gnutls_ffdhe_4096_group_prime = {0};
gnutls_datum_t gnutls_ffdhe_4096_group_q = {0};
unsigned int gnutls_ffdhe_4096_key_bits = {0};
gnutls_datum_t gnutls_ffdhe_6144_group_generator = {0};
gnutls_datum_t gnutls_ffdhe_6144_group_prime = {0};
gnutls_datum_t gnutls_ffdhe_6144_group_q = {0};
unsigned int gnutls_ffdhe_6144_key_bits = {0};
gnutls_datum_t gnutls_ffdhe_8192_group_generator = {0};
gnutls_datum_t gnutls_ffdhe_8192_group_prime = {0};
gnutls_datum_t gnutls_ffdhe_8192_group_q = {0};
unsigned int gnutls_ffdhe_8192_key_bits = {0};
gnutls_free_function gnutls_free = {0};
gnutls_alloc_function gnutls_malloc = {0};
gnutls_realloc_function gnutls_realloc = {0};
void *gnutls_secure_malloc = NULL;
gnutls_datum_t gnutls_srp_1024_group_generator = {0};
gnutls_datum_t gnutls_srp_1024_group_prime = {0};
gnutls_datum_t gnutls_srp_1536_group_generator = {0};
gnutls_datum_t gnutls_srp_1536_group_prime = {0};
gnutls_datum_t gnutls_srp_2048_group_generator = {0};
gnutls_datum_t gnutls_srp_2048_group_prime = {0};
gnutls_datum_t gnutls_srp_3072_group_generator = {0};
gnutls_datum_t gnutls_srp_3072_group_prime = {0};
gnutls_datum_t gnutls_srp_4096_group_generator = {0};
gnutls_datum_t gnutls_srp_4096_group_prime = {0};
gnutls_datum_t gnutls_srp_8192_group_generator = {0};
gnutls_datum_t gnutls_srp_8192_group_prime = {0};
char *(*gnutls_strdup)(const char *) = {0};

/* 前向声明（LOADER 在下方定义 LIB_sym） */
void *gnutls_sym(const char *name);

void gnutls_dyn_sync_globals(void) {
    void *p;
    (void)p;
    if ((p = gnutls_sym("gnutls_calloc")) != NULL) memcpy(&gnutls_calloc, p, sizeof(gnutls_calloc));
    if ((p = gnutls_sym("gnutls_ffdhe_2048_group_generator")) != NULL) memcpy(&gnutls_ffdhe_2048_group_generator, p, sizeof(gnutls_ffdhe_2048_group_generator));
    if ((p = gnutls_sym("gnutls_ffdhe_2048_group_prime")) != NULL) memcpy(&gnutls_ffdhe_2048_group_prime, p, sizeof(gnutls_ffdhe_2048_group_prime));
    if ((p = gnutls_sym("gnutls_ffdhe_2048_group_q")) != NULL) memcpy(&gnutls_ffdhe_2048_group_q, p, sizeof(gnutls_ffdhe_2048_group_q));
    if ((p = gnutls_sym("gnutls_ffdhe_2048_key_bits")) != NULL) memcpy(&gnutls_ffdhe_2048_key_bits, p, sizeof(gnutls_ffdhe_2048_key_bits));
    if ((p = gnutls_sym("gnutls_ffdhe_3072_group_generator")) != NULL) memcpy(&gnutls_ffdhe_3072_group_generator, p, sizeof(gnutls_ffdhe_3072_group_generator));
    if ((p = gnutls_sym("gnutls_ffdhe_3072_group_prime")) != NULL) memcpy(&gnutls_ffdhe_3072_group_prime, p, sizeof(gnutls_ffdhe_3072_group_prime));
    if ((p = gnutls_sym("gnutls_ffdhe_3072_group_q")) != NULL) memcpy(&gnutls_ffdhe_3072_group_q, p, sizeof(gnutls_ffdhe_3072_group_q));
    if ((p = gnutls_sym("gnutls_ffdhe_3072_key_bits")) != NULL) memcpy(&gnutls_ffdhe_3072_key_bits, p, sizeof(gnutls_ffdhe_3072_key_bits));
    if ((p = gnutls_sym("gnutls_ffdhe_4096_group_generator")) != NULL) memcpy(&gnutls_ffdhe_4096_group_generator, p, sizeof(gnutls_ffdhe_4096_group_generator));
    if ((p = gnutls_sym("gnutls_ffdhe_4096_group_prime")) != NULL) memcpy(&gnutls_ffdhe_4096_group_prime, p, sizeof(gnutls_ffdhe_4096_group_prime));
    if ((p = gnutls_sym("gnutls_ffdhe_4096_group_q")) != NULL) memcpy(&gnutls_ffdhe_4096_group_q, p, sizeof(gnutls_ffdhe_4096_group_q));
    if ((p = gnutls_sym("gnutls_ffdhe_4096_key_bits")) != NULL) memcpy(&gnutls_ffdhe_4096_key_bits, p, sizeof(gnutls_ffdhe_4096_key_bits));
    if ((p = gnutls_sym("gnutls_ffdhe_6144_group_generator")) != NULL) memcpy(&gnutls_ffdhe_6144_group_generator, p, sizeof(gnutls_ffdhe_6144_group_generator));
    if ((p = gnutls_sym("gnutls_ffdhe_6144_group_prime")) != NULL) memcpy(&gnutls_ffdhe_6144_group_prime, p, sizeof(gnutls_ffdhe_6144_group_prime));
    if ((p = gnutls_sym("gnutls_ffdhe_6144_group_q")) != NULL) memcpy(&gnutls_ffdhe_6144_group_q, p, sizeof(gnutls_ffdhe_6144_group_q));
    if ((p = gnutls_sym("gnutls_ffdhe_6144_key_bits")) != NULL) memcpy(&gnutls_ffdhe_6144_key_bits, p, sizeof(gnutls_ffdhe_6144_key_bits));
    if ((p = gnutls_sym("gnutls_ffdhe_8192_group_generator")) != NULL) memcpy(&gnutls_ffdhe_8192_group_generator, p, sizeof(gnutls_ffdhe_8192_group_generator));
    if ((p = gnutls_sym("gnutls_ffdhe_8192_group_prime")) != NULL) memcpy(&gnutls_ffdhe_8192_group_prime, p, sizeof(gnutls_ffdhe_8192_group_prime));
    if ((p = gnutls_sym("gnutls_ffdhe_8192_group_q")) != NULL) memcpy(&gnutls_ffdhe_8192_group_q, p, sizeof(gnutls_ffdhe_8192_group_q));
    if ((p = gnutls_sym("gnutls_ffdhe_8192_key_bits")) != NULL) memcpy(&gnutls_ffdhe_8192_key_bits, p, sizeof(gnutls_ffdhe_8192_key_bits));
    if ((p = gnutls_sym("gnutls_free")) != NULL) memcpy(&gnutls_free, p, sizeof(gnutls_free));
    if ((p = gnutls_sym("gnutls_malloc")) != NULL) memcpy(&gnutls_malloc, p, sizeof(gnutls_malloc));
    if ((p = gnutls_sym("gnutls_realloc")) != NULL) memcpy(&gnutls_realloc, p, sizeof(gnutls_realloc));
    if ((p = gnutls_sym("gnutls_secure_malloc")) != NULL) gnutls_secure_malloc = p;
    if ((p = gnutls_sym("gnutls_srp_1024_group_generator")) != NULL) memcpy(&gnutls_srp_1024_group_generator, p, sizeof(gnutls_srp_1024_group_generator));
    if ((p = gnutls_sym("gnutls_srp_1024_group_prime")) != NULL) memcpy(&gnutls_srp_1024_group_prime, p, sizeof(gnutls_srp_1024_group_prime));
    if ((p = gnutls_sym("gnutls_srp_1536_group_generator")) != NULL) memcpy(&gnutls_srp_1536_group_generator, p, sizeof(gnutls_srp_1536_group_generator));
    if ((p = gnutls_sym("gnutls_srp_1536_group_prime")) != NULL) memcpy(&gnutls_srp_1536_group_prime, p, sizeof(gnutls_srp_1536_group_prime));
    if ((p = gnutls_sym("gnutls_srp_2048_group_generator")) != NULL) memcpy(&gnutls_srp_2048_group_generator, p, sizeof(gnutls_srp_2048_group_generator));
    if ((p = gnutls_sym("gnutls_srp_2048_group_prime")) != NULL) memcpy(&gnutls_srp_2048_group_prime, p, sizeof(gnutls_srp_2048_group_prime));
    if ((p = gnutls_sym("gnutls_srp_3072_group_generator")) != NULL) memcpy(&gnutls_srp_3072_group_generator, p, sizeof(gnutls_srp_3072_group_generator));
    if ((p = gnutls_sym("gnutls_srp_3072_group_prime")) != NULL) memcpy(&gnutls_srp_3072_group_prime, p, sizeof(gnutls_srp_3072_group_prime));
    if ((p = gnutls_sym("gnutls_srp_4096_group_generator")) != NULL) memcpy(&gnutls_srp_4096_group_generator, p, sizeof(gnutls_srp_4096_group_generator));
    if ((p = gnutls_sym("gnutls_srp_4096_group_prime")) != NULL) memcpy(&gnutls_srp_4096_group_prime, p, sizeof(gnutls_srp_4096_group_prime));
    if ((p = gnutls_sym("gnutls_srp_8192_group_generator")) != NULL) memcpy(&gnutls_srp_8192_group_generator, p, sizeof(gnutls_srp_8192_group_generator));
    if ((p = gnutls_sym("gnutls_srp_8192_group_prime")) != NULL) memcpy(&gnutls_srp_8192_group_prime, p, sizeof(gnutls_srp_8192_group_prime));
    if ((p = gnutls_sym("gnutls_strdup")) != NULL) memcpy(&gnutls_strdup, p, sizeof(gnutls_strdup));
}

/* ============================================================
 * 全局状态与库加载
 * ============================================================ */
static lib_handle_t gnutls_handle = NULL;
static int gnutls_init_state = 0;

#define MAX_CANDIDATES 64
#define PATH_BUF_LEN   1024

static const char *gnutls_defaults[] = {
"libgnutls.so.30", "libgnutls.so", "libgnutls.dylib", "libgnutls-30.dll"
};

static int gnutls_do_load(const char *library_path) {
    if (gnutls_handle) return 0;
    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0, i;

    if (library_path && *library_path && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    const char *explicit_lib = getenv("GNUTLS_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", explicit_lib);
        candidates[n] = bufs[n]; n++;
    }
    const char *env_path = getenv(
#ifdef _WIN32
        "PATH"
#elif defined(__APPLE__)
        "DYLD_gnutlsRARY_PATH"
#else
        "LD_gnutlsRARY_PATH"
#endif
    );
    if (env_path && *env_path) {
        char *copy = strdup(env_path);
        if (copy) {
            char *p = copy;
            while (p && *p && n < MAX_CANDIDATES - 2) {
                char *sep = strchr(p, PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    for (i = 0; i < (int)(sizeof(gnutls_defaults)/sizeof(gnutls_defaults[0])); i++) {
                        if (n >= MAX_CANDIDATES - 2) break;
                        snprintf(bufs[n], PATH_BUF_LEN, "%s/%s", p, gnutls_defaults[i]);
                        candidates[n] = bufs[n]; n++;
                    }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }
    for (i = 0; i < (int)(sizeof(gnutls_defaults)/sizeof(gnutls_defaults[0])) && n < MAX_CANDIDATES - 1; i++)
        candidates[n++] = gnutls_defaults[i];
    const char *home = getenv("HOME");
    if (home && *home && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s/.nix-profile/lib/%s", home, gnutls_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    if (n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s", gnutls_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        gnutls_handle = load_lib(candidates[i]);
        if (gnutls_handle) break;
    }
    if (!gnutls_handle) {
        fprintf(stderr, "gnutls_dyn: cannot load library (%s)\n", lib_error());
        return -1;
    }
    if (!get_sym(gnutls_handle, "gnutls_aead_cipher_decrypt")) {
        fprintf(stderr, "gnutls_dyn: loaded library does not export %s\n", "gnutls_aead_cipher_decrypt");
        free_lib(gnutls_handle); gnutls_handle = NULL;
        return -1;
    }
    fprintf(stderr, "gnutls_dyn: loaded %d %s\n", i, candidates[i]);
    gnutls_dyn_sync_globals();
    return 0;
}

void *gnutls_sym(const char *name) { return gnutls_handle ? get_sym(gnutls_handle, name) : NULL; }

int gnutls_libok(void) {
    if (gnutls_init_state == 0)
        gnutls_init_state = (gnutls_do_load(NULL) == 0) ? 1 : -1;
    return (gnutls_init_state == 1 && gnutls_handle != NULL) ? 1 : 0;
}

int gnutls_dyn_init(const char *library_path) {
    if (gnutls_do_load(library_path) == 0) { gnutls_init_state = 1; return 0; }
    if (gnutls_init_state == 0) gnutls_init_state = -1;
    return -1;
}

void gnutls_dyn_cleanup(void) {
    /* 不卸载已成功加载的库（dlclose 不安全，且函数指针可能仍被持有） */
    if (!gnutls_handle) gnutls_init_state = 0;
    else if (gnutls_init_state == -1) gnutls_init_state = 0;
}

int gnutls_dyn_has_symbol(const char *name) {
    if (!gnutls_libok()) return 0;
    return gnutls_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏：首次调用 dlsym 并缓存；失败打印 stderr 并返回安全值
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!gnutls_libok()) return fallback;                                \
        *(void **)(&fn) = gnutls_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "gnutls_dyn: dlsym failed: %s\n", #name);        \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define WRAP_VOID(name, params, args)                                     \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!gnutls_libok()) return;                                         \
        *(void **)(&fn) = gnutls_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "gnutls_dyn: dlsym failed: %s\n", #name);        \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}
/* ---------- 全量函数封装 ---------- */
WRAP_RET(int, gnutls_aead_cipher_decrypt, (gnutls_aead_cipher_hd_t a0, const void * a1, size_t a2, const void * a3, size_t a4, size_t a5, const void * a6, size_t a7, void * a8, size_t * a9), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9), -1)
WRAP_RET(int, gnutls_aead_cipher_decryptv2, (gnutls_aead_cipher_hd_t a0, const void * a1, size_t a2, const giovec_t * a3, int a4, const giovec_t * a5, int a6, void * a7, size_t a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_VOID(gnutls_aead_cipher_deinit, (gnutls_aead_cipher_hd_t a0), (a0))
WRAP_RET(int, gnutls_aead_cipher_encrypt, (gnutls_aead_cipher_hd_t a0, const void * a1, size_t a2, const void * a3, size_t a4, size_t a5, const void * a6, size_t a7, void * a8, size_t * a9), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9), -1)
WRAP_RET(int, gnutls_aead_cipher_encryptv, (gnutls_aead_cipher_hd_t a0, const void * a1, size_t a2, const giovec_t * a3, int a4, size_t a5, const giovec_t * a6, int a7, void * a8, size_t * a9), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9), -1)
WRAP_RET(int, gnutls_aead_cipher_encryptv2, (gnutls_aead_cipher_hd_t a0, const void * a1, size_t a2, const giovec_t * a3, int a4, const giovec_t * a5, int a6, void * a7, size_t * a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_RET(int, gnutls_aead_cipher_init, (gnutls_aead_cipher_hd_t * a0, gnutls_cipher_algorithm_t a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_aead_cipher_set_key, (gnutls_aead_cipher_hd_t a0, const gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(gnutls_alert_description_t, gnutls_alert_get, (gnutls_session_t a0), (a0), ((gnutls_alert_description_t)0))
WRAP_RET(const char *, gnutls_alert_get_name, (gnutls_alert_description_t a0), (a0), NULL)
WRAP_RET(const char *, gnutls_alert_get_strname, (gnutls_alert_description_t a0), (a0), NULL)
WRAP_RET(int, gnutls_alert_send, (gnutls_session_t a0, gnutls_alert_level_t a1, gnutls_alert_description_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_alert_send_appropriate, (gnutls_session_t a0, int a1), (a0, a1), -1)
WRAP_VOID(gnutls_alert_set_read_function, (gnutls_session_t a0, gnutls_alert_read_func a1), (a0, a1))
WRAP_RET(int, gnutls_alpn_get_selected_protocol, (gnutls_session_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_alpn_set_protocols, (gnutls_session_t a0, const gnutls_datum_t * a1, unsigned int a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_anon_allocate_client_credentials, (gnutls_anon_client_credentials_t * a0), (a0), -1)
WRAP_RET(int, gnutls_anon_allocate_server_credentials, (gnutls_anon_server_credentials_t * a0), (a0), -1)
WRAP_VOID(gnutls_anon_free_client_credentials, (gnutls_anon_client_credentials_t a0), (a0))
WRAP_VOID(gnutls_anon_free_server_credentials, (gnutls_anon_server_credentials_t a0), (a0))
WRAP_VOID(gnutls_anon_set_params_function, (gnutls_anon_server_credentials_t a0, gnutls_params_function (*a1)), (a0, a1))
WRAP_VOID(gnutls_anon_set_server_dh_params, (gnutls_anon_server_credentials_t a0, gnutls_dh_params_t a1), (a0, a1))
WRAP_RET(int, gnutls_anon_set_server_known_dh_params, (gnutls_anon_server_credentials_t a0, gnutls_sec_param_t a1), (a0, a1), -1)
WRAP_VOID(gnutls_anon_set_server_params_function, (gnutls_anon_server_credentials_t a0, gnutls_params_function (*a1)), (a0, a1))
WRAP_VOID(gnutls_anti_replay_deinit, (gnutls_anti_replay_t a0), (a0))
WRAP_VOID(gnutls_anti_replay_enable, (gnutls_session_t a0, gnutls_anti_replay_t a1), (a0, a1))
WRAP_RET(int, gnutls_anti_replay_init, (gnutls_anti_replay_t * a0), (a0), -1)
WRAP_VOID(gnutls_anti_replay_set_add_function, (gnutls_anti_replay_t a0, gnutls_db_add_func a1), (a0, a1))
WRAP_VOID(gnutls_anti_replay_set_ptr, (gnutls_anti_replay_t a0, void * a1), (a0, a1))
WRAP_VOID(gnutls_anti_replay_set_window, (gnutls_anti_replay_t a0, unsigned int a1), (a0, a1))
WRAP_RET(long int, gnutls_audit_current_context, (), (), ((long int)0))
WRAP_VOID(gnutls_audit_pop_context, (), ())
WRAP_VOID(gnutls_audit_push_context, (long int a0), (a0))
WRAP_RET(gnutls_credentials_type_t, gnutls_auth_client_get_type, (gnutls_session_t a0), (a0), ((gnutls_credentials_type_t)0))
WRAP_RET(gnutls_credentials_type_t, gnutls_auth_get_type, (gnutls_session_t a0), (a0), ((gnutls_credentials_type_t)0))
WRAP_RET(gnutls_credentials_type_t, gnutls_auth_server_get_type, (gnutls_session_t a0), (a0), ((gnutls_credentials_type_t)0))
WRAP_RET(int, gnutls_base64_decode2, (const gnutls_datum_t * a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_base64_encode2, (const gnutls_datum_t * a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_buffer_append_data, (gnutls_buffer_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_bye, (gnutls_session_t a0, gnutls_close_request_t a1), (a0, a1), -1)
WRAP_RET(time_t, gnutls_certificate_activation_time_peers, (gnutls_session_t a0), (a0), ((time_t)0))
WRAP_RET(int, gnutls_certificate_allocate_credentials, (gnutls_certificate_credentials_t * a0), (a0), -1)
WRAP_RET(unsigned int, gnutls_certificate_client_get_request_status, (gnutls_session_t a0), (a0), 0)
WRAP_RET(time_t, gnutls_certificate_expiration_time_peers, (gnutls_session_t a0), (a0), ((time_t)0))
WRAP_VOID(gnutls_certificate_free_ca_names, (gnutls_certificate_credentials_t a0), (a0))
WRAP_VOID(gnutls_certificate_free_cas, (gnutls_certificate_credentials_t a0), (a0))
WRAP_VOID(gnutls_certificate_free_credentials, (gnutls_certificate_credentials_t a0), (a0))
WRAP_VOID(gnutls_certificate_free_crls, (gnutls_certificate_credentials_t a0), (a0))
WRAP_VOID(gnutls_certificate_free_keys, (gnutls_certificate_credentials_t a0), (a0))
WRAP_RET(int, gnutls_certificate_get_crt_raw, (gnutls_certificate_credentials_t a0, unsigned int a1, unsigned int a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_get_issuer, (gnutls_certificate_credentials_t a0, gnutls_x509_crt_t a1, gnutls_x509_crt_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(time_t, gnutls_certificate_get_ocsp_expiration, (gnutls_certificate_credentials_t a0, unsigned int a1, int a2, unsigned int a3), (a0, a1, a2, a3), ((time_t)0))
WRAP_RET(int, gnutls_certificate_get_openpgp_crt, (gnutls_certificate_credentials_t a0, unsigned int a1, gnutls_openpgp_crt_t ** a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_get_openpgp_key, (gnutls_certificate_credentials_t a0, unsigned int a1, gnutls_openpgp_privkey_t * a2), (a0, a1, a2), -1)
WRAP_RET(const gnutls_datum_t *, gnutls_certificate_get_ours, (gnutls_session_t a0), (a0), NULL)
WRAP_RET(const gnutls_datum_t *, gnutls_certificate_get_peers, (gnutls_session_t a0, unsigned int * a1), (a0, a1), NULL)
WRAP_RET(int, gnutls_certificate_get_peers_subkey_id, (gnutls_session_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_VOID(gnutls_certificate_get_trust_list, (gnutls_certificate_credentials_t a0, gnutls_x509_trust_list_t * a1), (a0, a1))
WRAP_RET(unsigned int, gnutls_certificate_get_verify_flags, (gnutls_certificate_credentials_t a0), (a0), 0)
WRAP_RET(int, gnutls_certificate_get_x509_crt, (gnutls_certificate_credentials_t a0, unsigned int a1, gnutls_x509_crt_t ** a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_get_x509_key, (gnutls_certificate_credentials_t a0, unsigned int a1, gnutls_x509_privkey_t * a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_certificate_send_x509_rdn_sequence, (gnutls_session_t a0, int a1), (a0, a1))
WRAP_VOID(gnutls_certificate_server_set_request, (gnutls_session_t a0, gnutls_certificate_request_t a1), (a0, a1))
WRAP_VOID(gnutls_certificate_set_dh_params, (gnutls_certificate_credentials_t a0, gnutls_dh_params_t a1), (a0, a1))
WRAP_VOID(gnutls_certificate_set_flags, (gnutls_certificate_credentials_t a0, unsigned int a1), (a0, a1))
WRAP_RET(int, gnutls_certificate_set_key, (gnutls_certificate_credentials_t a0, const char ** a1, int a2, gnutls_pcert_st * a3, int a4, gnutls_privkey_t a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_certificate_set_known_dh_params, (gnutls_certificate_credentials_t a0, gnutls_sec_param_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_certificate_set_ocsp_status_request_file, (gnutls_certificate_credentials_t a0, const char * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_certificate_set_ocsp_status_request_file2, (gnutls_certificate_credentials_t a0, const char * a1, unsigned int a2, gnutls_x509_crt_fmt_t a3), (a0, a1, a2, a3), -1)
WRAP_VOID(gnutls_certificate_set_ocsp_status_request_function, (gnutls_certificate_credentials_t a0, gnutls_status_request_ocsp_func a1, void * a2), (a0, a1, a2))
WRAP_RET(int, gnutls_certificate_set_ocsp_status_request_function2, (gnutls_certificate_credentials_t a0, unsigned int a1, gnutls_status_request_ocsp_func a2, void * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_set_ocsp_status_request_mem, (gnutls_certificate_credentials_t a0, const gnutls_datum_t * a1, unsigned int a2, gnutls_x509_crt_fmt_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_set_openpgp_key, (gnutls_certificate_credentials_t a0, gnutls_openpgp_crt_t a1, gnutls_openpgp_privkey_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_certificate_set_openpgp_key_file, (gnutls_certificate_credentials_t a0, const char * a1, const char * a2, gnutls_openpgp_crt_fmt_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_set_openpgp_key_file2, (gnutls_certificate_credentials_t a0, const char * a1, const char * a2, const char * a3, gnutls_openpgp_crt_fmt_t a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_certificate_set_openpgp_key_mem, (gnutls_certificate_credentials_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, gnutls_openpgp_crt_fmt_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_set_openpgp_key_mem2, (gnutls_certificate_credentials_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, const char * a3, gnutls_openpgp_crt_fmt_t a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_certificate_set_openpgp_keyring_file, (gnutls_certificate_credentials_t a0, const char * a1, gnutls_openpgp_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_certificate_set_openpgp_keyring_mem, (gnutls_certificate_credentials_t a0, const unsigned char * a1, size_t a2, gnutls_openpgp_crt_fmt_t a3), (a0, a1, a2, a3), -1)
WRAP_VOID(gnutls_certificate_set_params_function, (gnutls_certificate_credentials_t a0, gnutls_params_function (*a1)), (a0, a1))
WRAP_VOID(gnutls_certificate_set_pin_function, (gnutls_certificate_credentials_t a0, gnutls_pin_callback_t a1, void * a2), (a0, a1, a2))
WRAP_RET(int, gnutls_certificate_set_rawpk_key_file, (gnutls_certificate_credentials_t a0, const char * a1, const char * a2, gnutls_x509_crt_fmt_t a3, const char * a4, unsigned int a5, const char ** a6, unsigned int a7, unsigned int a8, unsigned int a9), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9), -1)
WRAP_RET(int, gnutls_certificate_set_rawpk_key_mem, (gnutls_certificate_credentials_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, gnutls_x509_crt_fmt_t a3, const char * a4, unsigned int a5, const char ** a6, unsigned int a7, unsigned int a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_VOID(gnutls_certificate_set_retrieve_function, (gnutls_certificate_credentials_t a0, gnutls_certificate_retrieve_function (*a1)), (a0, a1))
WRAP_VOID(gnutls_certificate_set_retrieve_function2, (gnutls_certificate_credentials_t a0, gnutls_certificate_retrieve_function2 (*a1)), (a0, a1))
WRAP_VOID(gnutls_certificate_set_retrieve_function3, (gnutls_certificate_credentials_t a0, gnutls_certificate_retrieve_function3 (*a1)), (a0, a1))
WRAP_VOID(gnutls_certificate_set_trust_list, (gnutls_certificate_credentials_t a0, gnutls_x509_trust_list_t a1, unsigned int a2), (a0, a1, a2))
WRAP_VOID(gnutls_certificate_set_verify_flags, (gnutls_certificate_credentials_t a0, unsigned int a1), (a0, a1))
WRAP_VOID(gnutls_certificate_set_verify_function, (gnutls_certificate_credentials_t a0, gnutls_certificate_verify_function (*a1)), (a0, a1))
WRAP_VOID(gnutls_certificate_set_verify_limits, (gnutls_certificate_credentials_t a0, unsigned int a1, unsigned int a2), (a0, a1, a2))
WRAP_RET(int, gnutls_certificate_set_x509_crl, (gnutls_certificate_credentials_t a0, gnutls_x509_crl_t * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_certificate_set_x509_crl_file, (gnutls_certificate_credentials_t a0, const char * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_certificate_set_x509_crl_mem, (gnutls_certificate_credentials_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_certificate_set_x509_key, (gnutls_certificate_credentials_t a0, gnutls_x509_crt_t * a1, int a2, gnutls_x509_privkey_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_set_x509_key_file, (gnutls_certificate_credentials_t a0, const char * a1, const char * a2, gnutls_x509_crt_fmt_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_set_x509_key_file2, (gnutls_certificate_credentials_t a0, const char * a1, const char * a2, gnutls_x509_crt_fmt_t a3, const char * a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_certificate_set_x509_key_mem, (gnutls_certificate_credentials_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, gnutls_x509_crt_fmt_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_set_x509_key_mem2, (gnutls_certificate_credentials_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, gnutls_x509_crt_fmt_t a3, const char * a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_certificate_set_x509_simple_pkcs12_file, (gnutls_certificate_credentials_t a0, const char * a1, gnutls_x509_crt_fmt_t a2, const char * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_set_x509_simple_pkcs12_mem, (gnutls_certificate_credentials_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2, const char * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_set_x509_system_trust, (gnutls_certificate_credentials_t a0), (a0), -1)
WRAP_RET(int, gnutls_certificate_set_x509_trust, (gnutls_certificate_credentials_t a0, gnutls_x509_crt_t * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_certificate_set_x509_trust_dir, (gnutls_certificate_credentials_t a0, const char * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_certificate_set_x509_trust_file, (gnutls_certificate_credentials_t a0, const char * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_certificate_set_x509_trust_mem, (gnutls_certificate_credentials_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(gnutls_certificate_type_t, gnutls_certificate_type_get, (gnutls_session_t a0), (a0), ((gnutls_certificate_type_t)0))
WRAP_RET(gnutls_certificate_type_t, gnutls_certificate_type_get2, (gnutls_session_t a0, gnutls_ctype_target_t a1), (a0, a1), ((gnutls_certificate_type_t)0))
WRAP_RET(gnutls_certificate_type_t, gnutls_certificate_type_get_id, (const char * a0), (a0), ((gnutls_certificate_type_t)0))
WRAP_RET(const char *, gnutls_certificate_type_get_name, (gnutls_certificate_type_t a0), (a0), NULL)
WRAP_RET(const gnutls_certificate_type_t *, gnutls_certificate_type_list, (), (), NULL)
WRAP_RET(gnutls_certificate_verification_profiles_t, gnutls_certificate_verification_profile_get_id, (const char * a0), (a0), ((gnutls_certificate_verification_profiles_t)0))
WRAP_RET(const char *, gnutls_certificate_verification_profile_get_name, (gnutls_certificate_verification_profiles_t a0), (a0), NULL)
WRAP_RET(int, gnutls_certificate_verification_status_print, (unsigned int a0, gnutls_certificate_type_t a1, gnutls_datum_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_verify_peers, (gnutls_session_t a0, gnutls_typed_vdata_st * a1, unsigned int a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_certificate_verify_peers2, (gnutls_session_t a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_certificate_verify_peers3, (gnutls_session_t a0, const char * a1, unsigned int * a2), (a0, a1, a2), -1)
WRAP_RET(const char *, gnutls_check_version, (const char * a0), (a0), NULL)
WRAP_RET(int, gnutls_cipher_add_auth, (gnutls_cipher_hd_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_cipher_decrypt, (const gnutls_cipher_hd_t a0, void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_cipher_decrypt2, (gnutls_cipher_hd_t a0, const void * a1, size_t a2, void * a3, size_t a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_cipher_decrypt3, (gnutls_cipher_hd_t a0, const void * a1, size_t a2, void * a3, size_t * a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_VOID(gnutls_cipher_deinit, (gnutls_cipher_hd_t a0), (a0))
WRAP_RET(int, gnutls_cipher_encrypt, (const gnutls_cipher_hd_t a0, void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_cipher_encrypt2, (gnutls_cipher_hd_t a0, const void * a1, size_t a2, void * a3, size_t a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_cipher_encrypt3, (gnutls_cipher_hd_t a0, const void * a1, size_t a2, void * a3, size_t * a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(gnutls_cipher_algorithm_t, gnutls_cipher_get, (gnutls_session_t a0), (a0), ((gnutls_cipher_algorithm_t)0))
WRAP_RET(unsigned int, gnutls_cipher_get_block_size, (gnutls_cipher_algorithm_t a0), (a0), 0)
WRAP_RET(gnutls_cipher_algorithm_t, gnutls_cipher_get_id, (const char * a0), (a0), ((gnutls_cipher_algorithm_t)0))
WRAP_RET(unsigned int, gnutls_cipher_get_iv_size, (gnutls_cipher_algorithm_t a0), (a0), 0)
WRAP_RET(size_t, gnutls_cipher_get_key_size, (gnutls_cipher_algorithm_t a0), (a0), 0)
WRAP_RET(const char *, gnutls_cipher_get_name, (gnutls_cipher_algorithm_t a0), (a0), NULL)
WRAP_RET(unsigned int, gnutls_cipher_get_tag_size, (gnutls_cipher_algorithm_t a0), (a0), 0)
WRAP_RET(int, gnutls_cipher_init, (gnutls_cipher_hd_t * a0, gnutls_cipher_algorithm_t a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(const gnutls_cipher_algorithm_t *, gnutls_cipher_list, (), (), NULL)
WRAP_RET(int, gnutls_cipher_self_test, (unsigned int a0, gnutls_cipher_algorithm_t a1), (a0, a1), -1)
WRAP_VOID(gnutls_cipher_set_iv, (gnutls_cipher_hd_t a0, void * a1, size_t a2), (a0, a1, a2))
WRAP_RET(const char *, gnutls_cipher_suite_get_name, (gnutls_kx_algorithm_t a0, gnutls_cipher_algorithm_t a1, gnutls_mac_algorithm_t a2), (a0, a1, a2), NULL)
WRAP_RET(const char *, gnutls_cipher_suite_info, (size_t a0, unsigned char * a1, gnutls_kx_algorithm_t * a2, gnutls_cipher_algorithm_t * a3, gnutls_mac_algorithm_t * a4, gnutls_protocol_t * a5), (a0, a1, a2, a3, a4, a5), NULL)
WRAP_RET(int, gnutls_cipher_tag, (gnutls_cipher_hd_t a0, void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(const char *, gnutls_ciphersuite_get, (gnutls_session_t a0), (a0), NULL)
WRAP_RET(gnutls_compression_method_t, gnutls_compress_certificate_get_selected_method, (gnutls_session_t a0), (a0), ((gnutls_compression_method_t)0))
WRAP_RET(int, gnutls_compress_certificate_set_methods, (gnutls_session_t a0, const gnutls_compression_method_t * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(gnutls_compression_method_t, gnutls_compression_get, (gnutls_session_t a0), (a0), ((gnutls_compression_method_t)0))
WRAP_RET(gnutls_compression_method_t, gnutls_compression_get_id, (const char * a0), (a0), ((gnutls_compression_method_t)0))
WRAP_RET(const char *, gnutls_compression_get_name, (gnutls_compression_method_t a0), (a0), NULL)
WRAP_RET(const gnutls_compression_method_t *, gnutls_compression_list, (), (), NULL)
WRAP_VOID(gnutls_credentials_clear, (gnutls_session_t a0), (a0))
WRAP_RET(int, gnutls_credentials_get, (gnutls_session_t a0, gnutls_credentials_type_t a1, void ** a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_credentials_set, (gnutls_session_t a0, gnutls_credentials_type_t a1, void * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_crypto_register_aead_cipher, (gnutls_cipher_algorithm_t a0, int a1, gnutls_cipher_init_func a2, gnutls_cipher_setkey_func a3, gnutls_cipher_aead_encrypt_func a4, gnutls_cipher_aead_decrypt_func a5, gnutls_cipher_deinit_func a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_crypto_register_cipher, (gnutls_cipher_algorithm_t a0, int a1, gnutls_cipher_init_func a2, gnutls_cipher_setkey_func a3, gnutls_cipher_setiv_func a4, gnutls_cipher_encrypt_func a5, gnutls_cipher_decrypt_func a6, gnutls_cipher_deinit_func a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(int, gnutls_crypto_register_digest, (gnutls_digest_algorithm_t a0, int a1, gnutls_digest_init_func a2, gnutls_digest_hash_func a3, gnutls_digest_output_func a4, gnutls_digest_deinit_func a5, gnutls_digest_fast_func a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_crypto_register_mac, (gnutls_mac_algorithm_t a0, int a1, gnutls_mac_init_func a2, gnutls_mac_setkey_func a3, gnutls_mac_setnonce_func a4, gnutls_mac_hash_func a5, gnutls_mac_output_func a6, gnutls_mac_deinit_func a7, gnutls_mac_fast_func a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_RET(int, gnutls_db_check_entry, (gnutls_session_t a0, gnutls_datum_t a1), (a0, a1), -1)
WRAP_RET(time_t, gnutls_db_check_entry_expire_time, (gnutls_datum_t * a0), (a0), ((time_t)0))
WRAP_RET(time_t, gnutls_db_check_entry_time, (gnutls_datum_t * a0), (a0), ((time_t)0))
WRAP_RET(unsigned int, gnutls_db_get_default_cache_expiration, (), (), 0)
WRAP_RET(void *, gnutls_db_get_ptr, (gnutls_session_t a0), (a0), NULL)
WRAP_VOID(gnutls_db_remove_session, (gnutls_session_t a0), (a0))
WRAP_VOID(gnutls_db_set_cache_expiration, (gnutls_session_t a0, int a1), (a0, a1))
WRAP_VOID(gnutls_db_set_ptr, (gnutls_session_t a0, void * a1), (a0, a1))
WRAP_VOID(gnutls_db_set_remove_function, (gnutls_session_t a0, gnutls_db_remove_func a1), (a0, a1))
WRAP_VOID(gnutls_db_set_retrieve_function, (gnutls_session_t a0, gnutls_db_retr_func a1), (a0, a1))
WRAP_VOID(gnutls_db_set_store_function, (gnutls_session_t a0, gnutls_db_store_func a1), (a0, a1))
WRAP_RET(int, gnutls_decode_ber_digest_info, (const gnutls_datum_t * a0, gnutls_digest_algorithm_t * a1, unsigned char * a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_decode_gost_rs_value, (const gnutls_datum_t * a0, gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_decode_rs_value, (const gnutls_datum_t * a0, gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_deinit, (gnutls_session_t a0), (a0))
WRAP_RET(int, gnutls_dh_get_group, (gnutls_session_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_dh_get_peers_public_bits, (gnutls_session_t a0), (a0), -1)
WRAP_RET(int, gnutls_dh_get_prime_bits, (gnutls_session_t a0), (a0), -1)
WRAP_RET(int, gnutls_dh_get_pubkey, (gnutls_session_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_dh_get_secret_bits, (gnutls_session_t a0), (a0), -1)
WRAP_RET(int, gnutls_dh_params_cpy, (gnutls_dh_params_t a0, gnutls_dh_params_t a1), (a0, a1), -1)
WRAP_VOID(gnutls_dh_params_deinit, (gnutls_dh_params_t a0), (a0))
WRAP_RET(int, gnutls_dh_params_export2_pkcs3, (gnutls_dh_params_t a0, gnutls_x509_crt_fmt_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_dh_params_export_pkcs3, (gnutls_dh_params_t a0, gnutls_x509_crt_fmt_t a1, unsigned char * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_dh_params_export_raw, (gnutls_dh_params_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_dh_params_generate2, (gnutls_dh_params_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_dh_params_import_dsa, (gnutls_dh_params_t a0, gnutls_x509_privkey_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_dh_params_import_pkcs3, (gnutls_dh_params_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_dh_params_import_raw, (gnutls_dh_params_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_dh_params_import_raw2, (gnutls_dh_params_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_dh_params_import_raw3, (gnutls_dh_params_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_dh_params_init, (gnutls_dh_params_t * a0), (a0), -1)
WRAP_VOID(gnutls_dh_set_prime_bits, (gnutls_session_t a0, unsigned int a1), (a0, a1))
WRAP_RET(gnutls_digest_algorithm_t, gnutls_digest_get_id, (const char * a0), (a0), ((gnutls_digest_algorithm_t)0))
WRAP_RET(const char *, gnutls_digest_get_name, (gnutls_digest_algorithm_t a0), (a0), NULL)
WRAP_RET(const char *, gnutls_digest_get_oid, (gnutls_digest_algorithm_t a0), (a0), NULL)
WRAP_RET(const gnutls_digest_algorithm_t *, gnutls_digest_list, (), (), NULL)
WRAP_RET(int, gnutls_digest_self_test, (unsigned int a0, gnutls_digest_algorithm_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_digest_set_secure, (gnutls_digest_algorithm_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_dtls_cookie_send, (gnutls_datum_t * a0, void * a1, size_t a2, gnutls_dtls_prestate_st * a3, gnutls_transport_ptr_t a4, gnutls_push_func a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_dtls_cookie_verify, (gnutls_datum_t * a0, void * a1, size_t a2, void * a3, size_t a4, gnutls_dtls_prestate_st * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(unsigned int, gnutls_dtls_get_data_mtu, (gnutls_session_t a0), (a0), 0)
WRAP_RET(unsigned int, gnutls_dtls_get_mtu, (gnutls_session_t a0), (a0), 0)
WRAP_RET(unsigned int, gnutls_dtls_get_timeout, (gnutls_session_t a0), (a0), 0)
WRAP_VOID(gnutls_dtls_prestate_set, (gnutls_session_t a0, gnutls_dtls_prestate_st * a1), (a0, a1))
WRAP_RET(int, gnutls_dtls_set_data_mtu, (gnutls_session_t a0, unsigned int a1), (a0, a1), -1)
WRAP_VOID(gnutls_dtls_set_mtu, (gnutls_session_t a0, unsigned int a1), (a0, a1))
WRAP_VOID(gnutls_dtls_set_timeouts, (gnutls_session_t a0, unsigned int a1, unsigned int a2), (a0, a1, a2))
WRAP_RET(gnutls_cipher_algorithm_t, gnutls_early_cipher_get, (gnutls_session_t a0), (a0), ((gnutls_cipher_algorithm_t)0))
WRAP_RET(gnutls_digest_algorithm_t, gnutls_early_prf_hash_get, (const gnutls_session_t a0), (a0), ((gnutls_digest_algorithm_t)0))
WRAP_RET(gnutls_ecc_curve_t, gnutls_ecc_curve_get, (gnutls_session_t a0), (a0), ((gnutls_ecc_curve_t)0))
WRAP_RET(gnutls_ecc_curve_t, gnutls_ecc_curve_get_id, (const char * a0), (a0), ((gnutls_ecc_curve_t)0))
WRAP_RET(const char *, gnutls_ecc_curve_get_name, (gnutls_ecc_curve_t a0), (a0), NULL)
WRAP_RET(const char *, gnutls_ecc_curve_get_oid, (gnutls_ecc_curve_t a0), (a0), NULL)
WRAP_RET(gnutls_pk_algorithm_t, gnutls_ecc_curve_get_pk, (gnutls_ecc_curve_t a0), (a0), ((gnutls_pk_algorithm_t)0))
WRAP_RET(int, gnutls_ecc_curve_get_size, (gnutls_ecc_curve_t a0), (a0), -1)
WRAP_RET(const gnutls_ecc_curve_t *, gnutls_ecc_curve_list, (), (), NULL)
WRAP_RET(int, gnutls_ecc_curve_set_enabled, (gnutls_ecc_curve_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_encode_ber_digest_info, (gnutls_digest_algorithm_t a0, const gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_encode_gost_rs_value, (gnutls_datum_t * a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_encode_rs_value, (gnutls_datum_t * a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_error_is_fatal, (int a0), (a0), -1)
WRAP_RET(int, gnutls_error_to_alert, (int a0, int * a1), (a0, a1), -1)
WRAP_RET(size_t, gnutls_est_record_overhead_size, (gnutls_protocol_t a0, gnutls_cipher_algorithm_t a1, gnutls_mac_algorithm_t a2, gnutls_compression_method_t a3, unsigned int a4), (a0, a1, a2, a3, a4), 0)
WRAP_RET(unsigned int, gnutls_ext_get_current_msg, (gnutls_session_t a0), (a0), 0)
WRAP_RET(int, gnutls_ext_get_data, (gnutls_session_t a0, unsigned int a1, gnutls_ext_priv_data_t * a2), (a0, a1, a2), -1)
WRAP_RET(const char *, gnutls_ext_get_name, (unsigned int a0), (a0), NULL)
WRAP_RET(const char *, gnutls_ext_get_name2, (gnutls_session_t a0, unsigned int a1, gnutls_ext_parse_type_t a2), (a0, a1, a2), NULL)
WRAP_RET(int, gnutls_ext_raw_parse, (void * a0, gnutls_ext_raw_process_func a1, const gnutls_datum_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_ext_register, (const char * a0, int a1, gnutls_ext_parse_type_t a2, gnutls_ext_recv_func a3, gnutls_ext_send_func a4, gnutls_ext_deinit_data_func a5, gnutls_ext_pack_func a6, gnutls_ext_unpack_func a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_VOID(gnutls_ext_set_data, (gnutls_session_t a0, unsigned int a1, gnutls_ext_priv_data_t a2), (a0, a1, a2))
WRAP_RET(int, gnutls_fingerprint, (gnutls_digest_algorithm_t a0, const gnutls_datum_t * a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_VOID(gnutls_fips140_context_deinit, (gnutls_fips140_context_t a0), (a0))
WRAP_RET(int, gnutls_fips140_context_init, (gnutls_fips140_context_t * a0), (a0), -1)
WRAP_RET(gnutls_fips140_operation_state_t, gnutls_fips140_get_operation_state, (gnutls_fips140_context_t a0), (a0), ((gnutls_fips140_operation_state_t)0))
WRAP_RET(unsigned int, gnutls_fips140_mode_enabled, (), (), 0)
WRAP_RET(int, gnutls_fips140_pop_context, (), (), -1)
WRAP_RET(int, gnutls_fips140_push_context, (gnutls_fips140_context_t a0), (a0), -1)
WRAP_RET(int, gnutls_fips140_run_self_tests, (), (), -1)
WRAP_VOID(gnutls_fips140_set_mode, (gnutls_fips_mode_t a0, unsigned int a1), (a0, a1))
WRAP_RET(const gnutls_library_config_st *, gnutls_get_library_config, (), (), NULL)
WRAP_RET(const char *, gnutls_get_system_config_file, (), (), NULL)
WRAP_VOID(gnutls_global_deinit, (), ())
WRAP_RET(int, gnutls_global_init, (), (), -1)
WRAP_VOID(gnutls_global_set_audit_log_function, (gnutls_audit_log_func a0), (a0))
WRAP_VOID(gnutls_global_set_log_function, (gnutls_log_func a0), (a0))
WRAP_VOID(gnutls_global_set_log_level, (int a0), (a0))
WRAP_VOID(gnutls_global_set_mem_functions, (gnutls_alloc_function a0, gnutls_alloc_function a1, gnutls_is_secure_function a2, gnutls_realloc_function a3, gnutls_free_function a4), (a0, a1, a2, a3, a4))
WRAP_VOID(gnutls_global_set_mutex, (mutex_init_func a0, mutex_deinit_func a1, mutex_lock_func a2, mutex_unlock_func a3), (a0, a1, a2, a3))
WRAP_VOID(gnutls_global_set_time_function, (gnutls_time_func a0), (a0))
WRAP_RET(const char *, gnutls_gost_paramset_get_name, (gnutls_gost_paramset_t a0), (a0), NULL)
WRAP_RET(const char *, gnutls_gost_paramset_get_oid, (gnutls_gost_paramset_t a0), (a0), NULL)
WRAP_RET(gnutls_group_t, gnutls_group_get, (gnutls_session_t a0), (a0), ((gnutls_group_t)0))
WRAP_RET(gnutls_group_t, gnutls_group_get_id, (const char * a0), (a0), ((gnutls_group_t)0))
WRAP_RET(const char *, gnutls_group_get_name, (gnutls_group_t a0), (a0), NULL)
WRAP_RET(const gnutls_group_t *, gnutls_group_list, (), (), NULL)
WRAP_RET(int, gnutls_handshake, (gnutls_session_t a0), (a0), -1)
WRAP_RET(const char *, gnutls_handshake_description_get_name, (gnutls_handshake_description_t a0), (a0), NULL)
WRAP_RET(gnutls_handshake_description_t, gnutls_handshake_get_last_in, (gnutls_session_t a0), (a0), ((gnutls_handshake_description_t)0))
WRAP_RET(gnutls_handshake_description_t, gnutls_handshake_get_last_out, (gnutls_session_t a0), (a0), ((gnutls_handshake_description_t)0))
WRAP_VOID(gnutls_handshake_set_hook_function, (gnutls_session_t a0, unsigned int a1, int a2, gnutls_handshake_hook_func a3), (a0, a1, a2, a3))
WRAP_VOID(gnutls_handshake_set_max_packet_length, (gnutls_session_t a0, size_t a1), (a0, a1))
WRAP_VOID(gnutls_handshake_set_post_client_hello_function, (gnutls_session_t a0, gnutls_handshake_simple_hook_func a1), (a0, a1))
WRAP_VOID(gnutls_handshake_set_private_extensions, (gnutls_session_t a0, int a1), (a0, a1))
WRAP_RET(int, gnutls_handshake_set_random, (gnutls_session_t a0, const gnutls_datum_t * a1), (a0, a1), -1)
WRAP_VOID(gnutls_handshake_set_read_function, (gnutls_session_t a0, gnutls_handshake_read_func a1), (a0, a1))
WRAP_VOID(gnutls_handshake_set_secret_function, (gnutls_session_t a0, gnutls_handshake_secret_func a1), (a0, a1))
WRAP_VOID(gnutls_handshake_set_timeout, (gnutls_session_t a0, unsigned int a1), (a0, a1))
WRAP_RET(int, gnutls_handshake_update_receiving_key, (gnutls_session_t a0), (a0), -1)
WRAP_RET(int, gnutls_handshake_write, (gnutls_session_t a0, gnutls_record_encryption_level_t a1, const void * a2, size_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_hash, (gnutls_hash_hd_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(gnutls_hash_hd_t, gnutls_hash_copy, (gnutls_hash_hd_t a0), (a0), ((gnutls_hash_hd_t)0))
WRAP_VOID(gnutls_hash_deinit, (gnutls_hash_hd_t a0, void * a1), (a0, a1))
WRAP_RET(int, gnutls_hash_fast, (gnutls_digest_algorithm_t a0, const void * a1, size_t a2, void * a3), (a0, a1, a2, a3), -1)
WRAP_RET(unsigned int, gnutls_hash_get_len, (gnutls_digest_algorithm_t a0), (a0), 0)
WRAP_RET(int, gnutls_hash_init, (gnutls_hash_hd_t * a0, gnutls_digest_algorithm_t a1), (a0, a1), -1)
WRAP_VOID(gnutls_hash_output, (gnutls_hash_hd_t a0, void * a1), (a0, a1))
WRAP_RET(int, gnutls_hash_squeeze, (gnutls_hash_hd_t a0, void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(unsigned int, gnutls_heartbeat_allowed, (gnutls_session_t a0, unsigned int a1), (a0, a1), 0)
WRAP_VOID(gnutls_heartbeat_enable, (gnutls_session_t a0, unsigned int a1), (a0, a1))
WRAP_RET(unsigned int, gnutls_heartbeat_get_timeout, (gnutls_session_t a0), (a0), 0)
WRAP_RET(int, gnutls_heartbeat_ping, (gnutls_session_t a0, size_t a1, unsigned int a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_heartbeat_pong, (gnutls_session_t a0, unsigned int a1), (a0, a1), -1)
WRAP_VOID(gnutls_heartbeat_set_timeouts, (gnutls_session_t a0, unsigned int a1, unsigned int a2), (a0, a1, a2))
WRAP_RET(int, gnutls_hex2bin, (const char * a0, size_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_hex_decode, (const gnutls_datum_t * a0, void * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_hex_decode2, (const gnutls_datum_t * a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_hex_encode, (const gnutls_datum_t * a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_hex_encode2, (const gnutls_datum_t * a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_hkdf_expand, (gnutls_mac_algorithm_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, void * a3, size_t a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_hkdf_extract, (gnutls_mac_algorithm_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, void * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_hkdf_self_test, (unsigned int a0, gnutls_mac_algorithm_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_hmac, (gnutls_hmac_hd_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(gnutls_hmac_hd_t, gnutls_hmac_copy, (gnutls_hmac_hd_t a0), (a0), ((gnutls_hmac_hd_t)0))
WRAP_VOID(gnutls_hmac_deinit, (gnutls_hmac_hd_t a0, void * a1), (a0, a1))
WRAP_RET(int, gnutls_hmac_fast, (gnutls_mac_algorithm_t a0, const void * a1, size_t a2, const void * a3, size_t a4, void * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(unsigned int, gnutls_hmac_get_key_size, (gnutls_mac_algorithm_t a0), (a0), 0)
WRAP_RET(unsigned int, gnutls_hmac_get_len, (gnutls_mac_algorithm_t a0), (a0), 0)
WRAP_RET(int, gnutls_hmac_init, (gnutls_hmac_hd_t * a0, gnutls_mac_algorithm_t a1, const void * a2, size_t a3), (a0, a1, a2, a3), -1)
WRAP_VOID(gnutls_hmac_output, (gnutls_hmac_hd_t a0, void * a1), (a0, a1))
WRAP_VOID(gnutls_hmac_set_nonce, (gnutls_hmac_hd_t a0, const void * a1, size_t a2), (a0, a1, a2))
WRAP_RET(int, gnutls_hpke_decap, (gnutls_hpke_context_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, const gnutls_privkey_t a3, const gnutls_pubkey_t a4, const gnutls_datum_t * a5, const gnutls_datum_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_hpke_deinit, (gnutls_hpke_context_t a0), (a0), -1)
WRAP_RET(int, gnutls_hpke_derive_keypair, (gnutls_hpke_kem_t a0, const gnutls_datum_t * a1, gnutls_privkey_t a2, gnutls_pubkey_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_hpke_encap, (gnutls_hpke_context_t a0, const gnutls_datum_t * a1, gnutls_datum_t * a2, const gnutls_pubkey_t a3, const gnutls_privkey_t a4, const gnutls_datum_t * a5, const gnutls_datum_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_hpke_export, (gnutls_hpke_context_t a0, const gnutls_datum_t * a1, size_t a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_hpke_export_pubkey, (const gnutls_hpke_context_t a0, const gnutls_pubkey_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_hpke_import_pubkey, (const gnutls_hpke_context_t a0, gnutls_pubkey_t a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_hpke_init, (gnutls_hpke_context_t * a0, gnutls_hpke_mode_t a1, gnutls_hpke_role_t a2, gnutls_hpke_kem_t a3, gnutls_hpke_kdf_t a4, gnutls_hpke_aead_t a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_hpke_open, (gnutls_hpke_context_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_hpke_seal, (gnutls_hpke_context_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_idna_map, (const char * a0, unsigned int a1, gnutls_datum_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_idna_reverse_map, (const char * a0, unsigned int a1, gnutls_datum_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_init, (gnutls_session_t * a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_key_generate, (gnutls_datum_t * a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(gnutls_kx_algorithm_t, gnutls_kx_get, (gnutls_session_t a0), (a0), ((gnutls_kx_algorithm_t)0))
WRAP_RET(gnutls_kx_algorithm_t, gnutls_kx_get_id, (const char * a0), (a0), ((gnutls_kx_algorithm_t)0))
WRAP_RET(const char *, gnutls_kx_get_name, (gnutls_kx_algorithm_t a0), (a0), NULL)
WRAP_RET(const gnutls_kx_algorithm_t *, gnutls_kx_list, (), (), NULL)
WRAP_RET(int, gnutls_load_file, (const char * a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(gnutls_mac_algorithm_t, gnutls_mac_get, (gnutls_session_t a0), (a0), ((gnutls_mac_algorithm_t)0))
WRAP_RET(gnutls_mac_algorithm_t, gnutls_mac_get_id, (const char * a0), (a0), ((gnutls_mac_algorithm_t)0))
WRAP_RET(size_t, gnutls_mac_get_key_size, (gnutls_mac_algorithm_t a0), (a0), 0)
WRAP_RET(const char *, gnutls_mac_get_name, (gnutls_mac_algorithm_t a0), (a0), NULL)
WRAP_RET(size_t, gnutls_mac_get_nonce_size, (gnutls_mac_algorithm_t a0), (a0), 0)
WRAP_RET(const gnutls_mac_algorithm_t *, gnutls_mac_list, (), (), NULL)
WRAP_RET(int, gnutls_mac_self_test, (unsigned int a0, gnutls_mac_algorithm_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_memcmp, (const void * a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_memset, (void * a0, int a1, size_t a2), (a0, a1, a2))
WRAP_RET(int, gnutls_ocsp_req_add_cert, (gnutls_ocsp_req_t a0, gnutls_digest_algorithm_t a1, gnutls_x509_crt_t a2, gnutls_x509_crt_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_ocsp_req_add_cert_id, (gnutls_ocsp_req_t a0, gnutls_digest_algorithm_t a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_VOID(gnutls_ocsp_req_deinit, (gnutls_ocsp_req_t a0), (a0))
WRAP_RET(int, gnutls_ocsp_req_export, (gnutls_ocsp_req_const_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_ocsp_req_get_cert_id, (gnutls_ocsp_req_const_t a0, unsigned int a1, gnutls_digest_algorithm_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_ocsp_req_get_extension, (gnutls_ocsp_req_const_t a0, unsigned int a1, gnutls_datum_t * a2, unsigned int * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_ocsp_req_get_nonce, (gnutls_ocsp_req_const_t a0, unsigned int * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_ocsp_req_get_version, (gnutls_ocsp_req_const_t a0), (a0), -1)
WRAP_RET(int, gnutls_ocsp_req_import, (gnutls_ocsp_req_t a0, const gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_ocsp_req_init, (gnutls_ocsp_req_t * a0), (a0), -1)
WRAP_RET(int, gnutls_ocsp_req_print, (gnutls_ocsp_req_const_t a0, gnutls_ocsp_print_formats_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_ocsp_req_randomize_nonce, (gnutls_ocsp_req_t a0), (a0), -1)
WRAP_RET(int, gnutls_ocsp_req_set_extension, (gnutls_ocsp_req_t a0, const char * a1, unsigned int a2, const gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_ocsp_req_set_nonce, (gnutls_ocsp_req_t a0, unsigned int a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_ocsp_resp_check_crt, (gnutls_ocsp_resp_const_t a0, unsigned int a1, gnutls_x509_crt_t a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_ocsp_resp_deinit, (gnutls_ocsp_resp_t a0), (a0))
WRAP_RET(int, gnutls_ocsp_resp_export, (gnutls_ocsp_resp_const_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_ocsp_resp_export2, (gnutls_ocsp_resp_const_t a0, gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_ocsp_resp_get_certs, (gnutls_ocsp_resp_const_t a0, gnutls_x509_crt_t ** a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_ocsp_resp_get_extension, (gnutls_ocsp_resp_const_t a0, unsigned int a1, gnutls_datum_t * a2, unsigned int * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_ocsp_resp_get_nonce, (gnutls_ocsp_resp_const_t a0, unsigned int * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(time_t, gnutls_ocsp_resp_get_produced, (gnutls_ocsp_resp_const_t a0), (a0), ((time_t)0))
WRAP_RET(int, gnutls_ocsp_resp_get_responder, (gnutls_ocsp_resp_const_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_ocsp_resp_get_responder2, (gnutls_ocsp_resp_const_t a0, gnutls_datum_t * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_ocsp_resp_get_responder_raw_id, (gnutls_ocsp_resp_const_t a0, unsigned int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_ocsp_resp_get_response, (gnutls_ocsp_resp_const_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_ocsp_resp_get_signature, (gnutls_ocsp_resp_const_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_ocsp_resp_get_signature_algorithm, (gnutls_ocsp_resp_const_t a0), (a0), -1)
WRAP_RET(int, gnutls_ocsp_resp_get_single, (gnutls_ocsp_resp_const_t a0, unsigned int a1, gnutls_digest_algorithm_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, unsigned int * a6, time_t * a7, time_t * a8, time_t * a9, unsigned int * a10), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10), -1)
WRAP_RET(int, gnutls_ocsp_resp_get_status, (gnutls_ocsp_resp_const_t a0), (a0), -1)
WRAP_RET(int, gnutls_ocsp_resp_get_version, (gnutls_ocsp_resp_const_t a0), (a0), -1)
WRAP_RET(int, gnutls_ocsp_resp_import, (gnutls_ocsp_resp_t a0, const gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_ocsp_resp_import2, (gnutls_ocsp_resp_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_ocsp_resp_init, (gnutls_ocsp_resp_t * a0), (a0), -1)
WRAP_RET(int, gnutls_ocsp_resp_list_import2, (gnutls_ocsp_resp_t ** a0, unsigned int * a1, const gnutls_datum_t * a2, gnutls_x509_crt_fmt_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_ocsp_resp_print, (gnutls_ocsp_resp_const_t a0, gnutls_ocsp_print_formats_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_ocsp_resp_verify, (gnutls_ocsp_resp_const_t a0, gnutls_x509_trust_list_t a1, unsigned int * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_ocsp_resp_verify_direct, (gnutls_ocsp_resp_const_t a0, gnutls_x509_crt_t a1, unsigned int * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_ocsp_status_request_enable_client, (gnutls_session_t a0, gnutls_datum_t * a1, size_t a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_ocsp_status_request_get, (gnutls_session_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_ocsp_status_request_get2, (gnutls_session_t a0, unsigned int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(unsigned int, gnutls_ocsp_status_request_is_checked, (gnutls_session_t a0, unsigned int a1), (a0, a1), 0)
WRAP_RET(gnutls_digest_algorithm_t, gnutls_oid_to_digest, (const char * a0), (a0), ((gnutls_digest_algorithm_t)0))
WRAP_RET(gnutls_ecc_curve_t, gnutls_oid_to_ecc_curve, (const char * a0), (a0), ((gnutls_ecc_curve_t)0))
WRAP_RET(gnutls_gost_paramset_t, gnutls_oid_to_gost_paramset, (const char * a0), (a0), ((gnutls_gost_paramset_t)0))
WRAP_RET(gnutls_mac_algorithm_t, gnutls_oid_to_mac, (const char * a0), (a0), ((gnutls_mac_algorithm_t)0))
WRAP_RET(gnutls_pk_algorithm_t, gnutls_oid_to_pk, (const char * a0), (a0), ((gnutls_pk_algorithm_t)0))
WRAP_RET(gnutls_sign_algorithm_t, gnutls_oid_to_sign, (const char * a0), (a0), ((gnutls_sign_algorithm_t)0))
WRAP_RET(int, gnutls_openpgp_crt_check_email, (gnutls_openpgp_crt_t a0, const char * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_crt_check_hostname, (gnutls_openpgp_crt_t a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_openpgp_crt_check_hostname2, (gnutls_openpgp_crt_t a0, const char * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_openpgp_crt_deinit, (gnutls_openpgp_crt_t a0), (a0))
WRAP_RET(int, gnutls_openpgp_crt_export, (gnutls_openpgp_crt_t a0, gnutls_openpgp_crt_fmt_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_openpgp_crt_export2, (gnutls_openpgp_crt_t a0, gnutls_openpgp_crt_fmt_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_auth_subkey, (gnutls_openpgp_crt_t a0, unsigned char * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(time_t, gnutls_openpgp_crt_get_creation_time, (gnutls_openpgp_crt_t a0), (a0), ((time_t)0))
WRAP_RET(time_t, gnutls_openpgp_crt_get_expiration_time, (gnutls_openpgp_crt_t a0), (a0), ((time_t)0))
WRAP_RET(int, gnutls_openpgp_crt_get_fingerprint, (gnutls_openpgp_crt_t a0, void * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_key_id, (gnutls_openpgp_crt_t a0, unsigned char * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_key_usage, (gnutls_openpgp_crt_t a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_name, (gnutls_openpgp_crt_t a0, int a1, char * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(gnutls_pk_algorithm_t, gnutls_openpgp_crt_get_pk_algorithm, (gnutls_openpgp_crt_t a0, unsigned int * a1), (a0, a1), ((gnutls_pk_algorithm_t)0))
WRAP_RET(int, gnutls_openpgp_crt_get_pk_dsa_raw, (gnutls_openpgp_crt_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_pk_rsa_raw, (gnutls_openpgp_crt_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_preferred_key_id, (gnutls_openpgp_crt_t a0, unsigned char * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_revoked_status, (gnutls_openpgp_crt_t a0), (a0), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_subkey_count, (gnutls_openpgp_crt_t a0), (a0), -1)
WRAP_RET(time_t, gnutls_openpgp_crt_get_subkey_creation_time, (gnutls_openpgp_crt_t a0, unsigned int a1), (a0, a1), ((time_t)0))
WRAP_RET(time_t, gnutls_openpgp_crt_get_subkey_expiration_time, (gnutls_openpgp_crt_t a0, unsigned int a1), (a0, a1), ((time_t)0))
WRAP_RET(int, gnutls_openpgp_crt_get_subkey_fingerprint, (gnutls_openpgp_crt_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_subkey_id, (gnutls_openpgp_crt_t a0, unsigned int a1, unsigned char * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_subkey_idx, (gnutls_openpgp_crt_t a0, const unsigned char * a1), (a0, a1), -1)
WRAP_RET(gnutls_pk_algorithm_t, gnutls_openpgp_crt_get_subkey_pk_algorithm, (gnutls_openpgp_crt_t a0, unsigned int a1, unsigned int * a2), (a0, a1, a2), ((gnutls_pk_algorithm_t)0))
WRAP_RET(int, gnutls_openpgp_crt_get_subkey_pk_dsa_raw, (gnutls_openpgp_crt_t a0, unsigned int a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_subkey_pk_rsa_raw, (gnutls_openpgp_crt_t a0, unsigned int a1, gnutls_datum_t * a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_subkey_revoked_status, (gnutls_openpgp_crt_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_subkey_usage, (gnutls_openpgp_crt_t a0, unsigned int a1, unsigned int * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_crt_get_version, (gnutls_openpgp_crt_t a0), (a0), -1)
WRAP_RET(int, gnutls_openpgp_crt_import, (gnutls_openpgp_crt_t a0, const gnutls_datum_t * a1, gnutls_openpgp_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_crt_init, (gnutls_openpgp_crt_t * a0), (a0), -1)
WRAP_RET(int, gnutls_openpgp_crt_print, (gnutls_openpgp_crt_t a0, gnutls_certificate_print_formats_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_crt_set_preferred_key_id, (gnutls_openpgp_crt_t a0, const unsigned char * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_openpgp_crt_verify_ring, (gnutls_openpgp_crt_t a0, gnutls_openpgp_keyring_t a1, unsigned int a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_openpgp_crt_verify_self, (gnutls_openpgp_crt_t a0, unsigned int a1, unsigned int * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_keyring_check_id, (gnutls_openpgp_keyring_t a0, const unsigned char * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_openpgp_keyring_deinit, (gnutls_openpgp_keyring_t a0), (a0))
WRAP_RET(int, gnutls_openpgp_keyring_get_crt, (gnutls_openpgp_keyring_t a0, unsigned int a1, gnutls_openpgp_crt_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_keyring_get_crt_count, (gnutls_openpgp_keyring_t a0), (a0), -1)
WRAP_RET(int, gnutls_openpgp_keyring_import, (gnutls_openpgp_keyring_t a0, const gnutls_datum_t * a1, gnutls_openpgp_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_keyring_init, (gnutls_openpgp_keyring_t * a0), (a0), -1)
WRAP_VOID(gnutls_openpgp_privkey_deinit, (gnutls_openpgp_privkey_t a0), (a0))
WRAP_RET(int, gnutls_openpgp_privkey_export, (gnutls_openpgp_privkey_t a0, gnutls_openpgp_crt_fmt_t a1, const char * a2, unsigned int a3, void * a4, size_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_openpgp_privkey_export2, (gnutls_openpgp_privkey_t a0, gnutls_openpgp_crt_fmt_t a1, const char * a2, unsigned int a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_openpgp_privkey_export_dsa_raw, (gnutls_openpgp_privkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_openpgp_privkey_export_rsa_raw, (gnutls_openpgp_privkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, gnutls_datum_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_openpgp_privkey_export_subkey_dsa_raw, (gnutls_openpgp_privkey_t a0, unsigned int a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, gnutls_datum_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_openpgp_privkey_export_subkey_rsa_raw, (gnutls_openpgp_privkey_t a0, unsigned int a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, gnutls_datum_t * a6, gnutls_datum_t * a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(int, gnutls_openpgp_privkey_get_fingerprint, (gnutls_openpgp_privkey_t a0, void * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_privkey_get_key_id, (gnutls_openpgp_privkey_t a0, unsigned char * a1), (a0, a1), -1)
WRAP_RET(gnutls_pk_algorithm_t, gnutls_openpgp_privkey_get_pk_algorithm, (gnutls_openpgp_privkey_t a0, unsigned int * a1), (a0, a1), ((gnutls_pk_algorithm_t)0))
WRAP_RET(int, gnutls_openpgp_privkey_get_preferred_key_id, (gnutls_openpgp_privkey_t a0, unsigned char * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_openpgp_privkey_get_revoked_status, (gnutls_openpgp_privkey_t a0), (a0), -1)
WRAP_RET(int, gnutls_openpgp_privkey_get_subkey_count, (gnutls_openpgp_privkey_t a0), (a0), -1)
WRAP_RET(time_t, gnutls_openpgp_privkey_get_subkey_creation_time, (gnutls_openpgp_privkey_t a0, unsigned int a1), (a0, a1), ((time_t)0))
WRAP_RET(time_t, gnutls_openpgp_privkey_get_subkey_expiration_time, (gnutls_openpgp_privkey_t a0, unsigned int a1), (a0, a1), ((time_t)0))
WRAP_RET(int, gnutls_openpgp_privkey_get_subkey_fingerprint, (gnutls_openpgp_privkey_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_openpgp_privkey_get_subkey_id, (gnutls_openpgp_privkey_t a0, unsigned int a1, unsigned char * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_openpgp_privkey_get_subkey_idx, (gnutls_openpgp_privkey_t a0, const unsigned char * a1), (a0, a1), -1)
WRAP_RET(gnutls_pk_algorithm_t, gnutls_openpgp_privkey_get_subkey_pk_algorithm, (gnutls_openpgp_privkey_t a0, unsigned int a1, unsigned int * a2), (a0, a1, a2), ((gnutls_pk_algorithm_t)0))
WRAP_RET(int, gnutls_openpgp_privkey_get_subkey_revoked_status, (gnutls_openpgp_privkey_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_openpgp_privkey_import, (gnutls_openpgp_privkey_t a0, const gnutls_datum_t * a1, gnutls_openpgp_crt_fmt_t a2, const char * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_openpgp_privkey_init, (gnutls_openpgp_privkey_t * a0), (a0), -1)
WRAP_RET(gnutls_sec_param_t, gnutls_openpgp_privkey_sec_param, (gnutls_openpgp_privkey_t a0), (a0), ((gnutls_sec_param_t)0))
WRAP_RET(int, gnutls_openpgp_privkey_set_preferred_key_id, (gnutls_openpgp_privkey_t a0, const unsigned char * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_openpgp_privkey_sign_hash, (gnutls_openpgp_privkey_t a0, const gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_openpgp_send_cert, (gnutls_session_t a0, gnutls_openpgp_crt_status_t a1), (a0, a1))
WRAP_VOID(gnutls_openpgp_set_recv_key_function, (gnutls_session_t a0, gnutls_openpgp_recv_key_func a1), (a0, a1))
WRAP_VOID(gnutls_packet_deinit, (gnutls_packet_t a0), (a0))
WRAP_VOID(gnutls_packet_get, (gnutls_packet_t a0, gnutls_datum_t * a1, unsigned char * a2), (a0, a1, a2))
WRAP_RET(int, gnutls_pbkdf2, (gnutls_mac_algorithm_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, unsigned int a3, void * a4, size_t a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_pbkdf2_self_test, (unsigned int a0, gnutls_mac_algorithm_t a1), (a0, a1), -1)
WRAP_VOID(gnutls_pcert_deinit, (gnutls_pcert_st * a0), (a0))
WRAP_RET(int, gnutls_pcert_export_openpgp, (gnutls_pcert_st * a0, gnutls_openpgp_crt_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pcert_export_x509, (gnutls_pcert_st * a0, gnutls_x509_crt_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pcert_import_openpgp, (gnutls_pcert_st * a0, gnutls_openpgp_crt_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pcert_import_openpgp_raw, (gnutls_pcert_st * a0, const gnutls_datum_t * a1, gnutls_openpgp_crt_fmt_t a2, unsigned char * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pcert_import_rawpk, (gnutls_pcert_st * a0, gnutls_pubkey_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pcert_import_rawpk_raw, (gnutls_pcert_st * a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2, unsigned int a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pcert_import_x509, (gnutls_pcert_st * a0, gnutls_x509_crt_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pcert_import_x509_list, (gnutls_pcert_st * a0, gnutls_x509_crt_t * a1, unsigned int * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pcert_import_x509_raw, (gnutls_pcert_st * a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pcert_list_import_x509_file, (gnutls_pcert_st * a0, unsigned int * a1, const char * a2, gnutls_x509_crt_fmt_t a3, gnutls_pin_callback_t a4, void * a5, unsigned int a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_pcert_list_import_x509_raw, (gnutls_pcert_st * a0, unsigned int * a1, const gnutls_datum_t * a2, gnutls_x509_crt_fmt_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pem_base64_decode, (const char * a0, const gnutls_datum_t * a1, unsigned char * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pem_base64_decode2, (const char * a0, const gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pem_base64_encode, (const char * a0, const gnutls_datum_t * a1, char * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pem_base64_encode2, (const char * a0, const gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_perror, (int a0), (a0))
WRAP_RET(const char *, gnutls_pk_algorithm_get_name, (gnutls_pk_algorithm_t a0), (a0), NULL)
WRAP_RET(gnutls_sec_param_t, gnutls_pk_bits_to_sec_param, (gnutls_pk_algorithm_t a0, unsigned int a1), (a0, a1), ((gnutls_sec_param_t)0))
WRAP_RET(gnutls_pk_algorithm_t, gnutls_pk_get_id, (const char * a0), (a0), ((gnutls_pk_algorithm_t)0))
WRAP_RET(const char *, gnutls_pk_get_name, (gnutls_pk_algorithm_t a0), (a0), NULL)
WRAP_RET(const char *, gnutls_pk_get_oid, (gnutls_pk_algorithm_t a0), (a0), NULL)
WRAP_RET(const gnutls_pk_algorithm_t *, gnutls_pk_list, (), (), NULL)
WRAP_RET(int, gnutls_pk_self_test, (unsigned int a0, gnutls_pk_algorithm_t a1), (a0, a1), -1)
WRAP_RET(gnutls_sign_algorithm_t, gnutls_pk_to_sign, (gnutls_pk_algorithm_t a0, gnutls_digest_algorithm_t a1), (a0, a1), ((gnutls_sign_algorithm_t)0))
WRAP_RET(int, gnutls_pkcs11_add_provider, (const char * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs11_copy_attached_extension, (const char * a0, gnutls_x509_crt_t a1, gnutls_datum_t * a2, const char * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pkcs11_copy_pubkey, (const char * a0, gnutls_pubkey_t a1, const char * a2, const gnutls_datum_t * a3, unsigned int a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_pkcs11_copy_secret_key, (const char * a0, gnutls_datum_t * a1, const char * a2, unsigned int a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pkcs11_copy_x509_crt2, (const char * a0, gnutls_x509_crt_t a1, const char * a2, const gnutls_datum_t * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pkcs11_copy_x509_privkey2, (const char * a0, gnutls_x509_privkey_t a1, const char * a2, const gnutls_datum_t * a3, unsigned int a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(unsigned int, gnutls_pkcs11_crt_is_known, (const char * a0, gnutls_x509_crt_t a1, unsigned int a2), (a0, a1, a2), 0)
WRAP_VOID(gnutls_pkcs11_deinit, (), ())
WRAP_RET(int, gnutls_pkcs11_delete_url, (const char * a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(gnutls_pin_callback_t, gnutls_pkcs11_get_pin_function, (void ** a0), (a0), ((gnutls_pin_callback_t)0))
WRAP_RET(int, gnutls_pkcs11_get_raw_issuer, (const char * a0, gnutls_x509_crt_t a1, gnutls_datum_t * a2, gnutls_x509_crt_fmt_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pkcs11_get_raw_issuer_by_dn, (const char * a0, const gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_x509_crt_fmt_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pkcs11_get_raw_issuer_by_subject_key_id, (const char * a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_x509_crt_fmt_t a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_pkcs11_init, (unsigned int a0, const char * a1), (a0, a1), -1)
WRAP_VOID(gnutls_pkcs11_obj_deinit, (gnutls_pkcs11_obj_t a0), (a0))
WRAP_RET(int, gnutls_pkcs11_obj_export, (gnutls_pkcs11_obj_t a0, void * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs11_obj_export2, (gnutls_pkcs11_obj_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs11_obj_export3, (gnutls_pkcs11_obj_t a0, gnutls_x509_crt_fmt_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs11_obj_export_url, (gnutls_pkcs11_obj_t a0, gnutls_pkcs11_url_type_t a1, char ** a2), (a0, a1, a2), -1)
WRAP_RET(char *, gnutls_pkcs11_obj_flags_get_str, (unsigned int a0), (a0), NULL)
WRAP_RET(int, gnutls_pkcs11_obj_get_exts, (gnutls_pkcs11_obj_t a0, struct gnutls_x509_ext_st ** a1, unsigned int * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs11_obj_get_flags, (gnutls_pkcs11_obj_t a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs11_obj_get_info, (gnutls_pkcs11_obj_t a0, gnutls_pkcs11_obj_info_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs11_obj_get_pk_algorithm, (gnutls_pkcs11_obj_t a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs11_obj_get_ptr, (gnutls_pkcs11_obj_t a0, void ** a1, void ** a2, void ** a3, long unsigned int * a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(gnutls_pkcs11_obj_type_t, gnutls_pkcs11_obj_get_type, (gnutls_pkcs11_obj_t a0), (a0), ((gnutls_pkcs11_obj_type_t)0))
WRAP_RET(int, gnutls_pkcs11_obj_import_url, (gnutls_pkcs11_obj_t a0, const char * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs11_obj_init, (gnutls_pkcs11_obj_t * a0), (a0), -1)
WRAP_RET(int, gnutls_pkcs11_obj_list_import_url3, (gnutls_pkcs11_obj_t * a0, unsigned int *const a1, const char * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs11_obj_list_import_url4, (gnutls_pkcs11_obj_t ** a0, unsigned int * a1, const char * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs11_obj_set_info, (gnutls_pkcs11_obj_t a0, gnutls_pkcs11_obj_info_t a1, const void * a2, size_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_VOID(gnutls_pkcs11_obj_set_pin_function, (gnutls_pkcs11_obj_t a0, gnutls_pin_callback_t a1, void * a2), (a0, a1, a2))
WRAP_RET(int, gnutls_pkcs11_privkey_cpy, (gnutls_pkcs11_privkey_t a0, gnutls_pkcs11_privkey_t a1), (a0, a1), -1)
WRAP_VOID(gnutls_pkcs11_privkey_deinit, (gnutls_pkcs11_privkey_t a0), (a0))
WRAP_RET(int, gnutls_pkcs11_privkey_export_pubkey, (gnutls_pkcs11_privkey_t a0, gnutls_x509_crt_fmt_t a1, gnutls_datum_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs11_privkey_export_url, (gnutls_pkcs11_privkey_t a0, gnutls_pkcs11_url_type_t a1, char ** a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs11_privkey_generate3, (const char * a0, gnutls_pk_algorithm_t a1, unsigned int a2, const char * a3, const gnutls_datum_t * a4, gnutls_x509_crt_fmt_t a5, gnutls_datum_t * a6, unsigned int a7, unsigned int a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_RET(int, gnutls_pkcs11_privkey_get_info, (gnutls_pkcs11_privkey_t a0, gnutls_pkcs11_obj_info_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs11_privkey_get_pk_algorithm, (gnutls_pkcs11_privkey_t a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs11_privkey_import_url, (gnutls_pkcs11_privkey_t a0, const char * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs11_privkey_init, (gnutls_pkcs11_privkey_t * a0), (a0), -1)
WRAP_VOID(gnutls_pkcs11_privkey_set_pin_function, (gnutls_pkcs11_privkey_t a0, gnutls_pin_callback_t a1, void * a2), (a0, a1, a2))
WRAP_RET(unsigned int, gnutls_pkcs11_privkey_status, (gnutls_pkcs11_privkey_t a0), (a0), 0)
WRAP_RET(int, gnutls_pkcs11_reinit, (), (), -1)
WRAP_VOID(gnutls_pkcs11_set_pin_function, (gnutls_pin_callback_t a0, void * a1), (a0, a1))
WRAP_VOID(gnutls_pkcs11_set_token_function, (gnutls_pkcs11_token_callback_t a0, void * a1), (a0, a1))
WRAP_RET(unsigned int, gnutls_pkcs11_token_check_mechanism, (const char * a0, long unsigned int a1, void * a2, unsigned int a3, unsigned int a4), (a0, a1, a2, a3, a4), 0)
WRAP_RET(int, gnutls_pkcs11_token_get_flags, (const char * a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs11_token_get_info, (const char * a0, gnutls_pkcs11_token_info_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs11_token_get_mechanism, (const char * a0, unsigned int a1, long unsigned int * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs11_token_get_ptr, (const char * a0, void ** a1, long unsigned int * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs11_token_get_random, (const char * a0, void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs11_token_get_url, (unsigned int a0, gnutls_pkcs11_url_type_t a1, char ** a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs11_token_init, (const char * a0, const char * a1, const char * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs11_token_set_pin, (const char * a0, const char * a1, const char * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(const char *, gnutls_pkcs11_type_get_name, (gnutls_pkcs11_obj_type_t a0), (a0), NULL)
WRAP_RET(int, gnutls_pkcs12_bag_decrypt, (gnutls_pkcs12_bag_t a0, const char * a1), (a0, a1), -1)
WRAP_VOID(gnutls_pkcs12_bag_deinit, (gnutls_pkcs12_bag_t a0), (a0))
WRAP_RET(int, gnutls_pkcs12_bag_enc_info, (gnutls_pkcs12_bag_t a0, unsigned int * a1, unsigned int * a2, void * a3, unsigned int * a4, unsigned int * a5, char ** a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_pkcs12_bag_encrypt, (gnutls_pkcs12_bag_t a0, const char * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs12_bag_get_count, (gnutls_pkcs12_bag_t a0), (a0), -1)
WRAP_RET(int, gnutls_pkcs12_bag_get_data, (gnutls_pkcs12_bag_t a0, unsigned int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs12_bag_get_friendly_name, (gnutls_pkcs12_bag_t a0, unsigned int a1, char ** a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs12_bag_get_key_id, (gnutls_pkcs12_bag_t a0, unsigned int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs12_bag_get_type, (gnutls_pkcs12_bag_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs12_bag_init, (gnutls_pkcs12_bag_t * a0), (a0), -1)
WRAP_RET(int, gnutls_pkcs12_bag_set_crl, (gnutls_pkcs12_bag_t a0, gnutls_x509_crl_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs12_bag_set_crt, (gnutls_pkcs12_bag_t a0, gnutls_x509_crt_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs12_bag_set_data, (gnutls_pkcs12_bag_t a0, gnutls_pkcs12_bag_type_t a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs12_bag_set_friendly_name, (gnutls_pkcs12_bag_t a0, unsigned int a1, const char * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs12_bag_set_key_id, (gnutls_pkcs12_bag_t a0, unsigned int a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs12_bag_set_privkey, (gnutls_pkcs12_bag_t a0, gnutls_x509_privkey_t a1, const char * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_VOID(gnutls_pkcs12_deinit, (gnutls_pkcs12_t a0), (a0))
WRAP_RET(int, gnutls_pkcs12_export, (gnutls_pkcs12_t a0, gnutls_x509_crt_fmt_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs12_export2, (gnutls_pkcs12_t a0, gnutls_x509_crt_fmt_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs12_generate_mac, (gnutls_pkcs12_t a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs12_generate_mac2, (gnutls_pkcs12_t a0, gnutls_mac_algorithm_t a1, const char * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs12_generate_mac3, (gnutls_pkcs12_t a0, gnutls_mac_algorithm_t a1, const char * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs12_get_bag, (gnutls_pkcs12_t a0, int a1, gnutls_pkcs12_bag_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs12_import, (gnutls_pkcs12_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs12_init, (gnutls_pkcs12_t * a0), (a0), -1)
WRAP_RET(int, gnutls_pkcs12_mac_info, (gnutls_pkcs12_t a0, unsigned int * a1, void * a2, unsigned int * a3, unsigned int * a4, char ** a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_pkcs12_set_bag, (gnutls_pkcs12_t a0, gnutls_pkcs12_bag_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs12_simple_parse, (gnutls_pkcs12_t a0, const char * a1, gnutls_x509_privkey_t * a2, gnutls_x509_crt_t ** a3, unsigned int * a4, gnutls_x509_crt_t ** a5, unsigned int * a6, gnutls_x509_crl_t * a7, unsigned int a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_RET(int, gnutls_pkcs12_verify_mac, (gnutls_pkcs12_t a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs7_add_attr, (gnutls_pkcs7_attrs_t * a0, const char * a1, gnutls_datum_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_VOID(gnutls_pkcs7_attrs_deinit, (gnutls_pkcs7_attrs_t a0), (a0))
WRAP_VOID(gnutls_pkcs7_deinit, (gnutls_pkcs7_t a0), (a0))
WRAP_RET(int, gnutls_pkcs7_delete_crl, (gnutls_pkcs7_t a0, int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs7_delete_crt, (gnutls_pkcs7_t a0, int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs7_export, (gnutls_pkcs7_t a0, gnutls_x509_crt_fmt_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs7_export2, (gnutls_pkcs7_t a0, gnutls_x509_crt_fmt_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs7_get_attr, (gnutls_pkcs7_attrs_t a0, unsigned int a1, char ** a2, gnutls_datum_t * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pkcs7_get_crl_count, (gnutls_pkcs7_t a0), (a0), -1)
WRAP_RET(int, gnutls_pkcs7_get_crl_raw, (gnutls_pkcs7_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs7_get_crl_raw2, (gnutls_pkcs7_t a0, unsigned int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs7_get_crt_count, (gnutls_pkcs7_t a0), (a0), -1)
WRAP_RET(int, gnutls_pkcs7_get_crt_raw, (gnutls_pkcs7_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pkcs7_get_crt_raw2, (gnutls_pkcs7_t a0, unsigned int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs7_get_embedded_data, (gnutls_pkcs7_t a0, unsigned int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(const char *, gnutls_pkcs7_get_embedded_data_oid, (gnutls_pkcs7_t a0), (a0), NULL)
WRAP_RET(int, gnutls_pkcs7_get_signature_count, (gnutls_pkcs7_t a0), (a0), -1)
WRAP_RET(int, gnutls_pkcs7_get_signature_info, (gnutls_pkcs7_t a0, unsigned int a1, gnutls_pkcs7_signature_info_st * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs7_import, (gnutls_pkcs7_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs7_init, (gnutls_pkcs7_t * a0), (a0), -1)
WRAP_RET(int, gnutls_pkcs7_print, (gnutls_pkcs7_t a0, gnutls_certificate_print_formats_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs7_print_signature_info, (gnutls_pkcs7_signature_info_st * a0, gnutls_certificate_print_formats_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pkcs7_set_crl, (gnutls_pkcs7_t a0, gnutls_x509_crl_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs7_set_crl_raw, (gnutls_pkcs7_t a0, const gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs7_set_crt, (gnutls_pkcs7_t a0, gnutls_x509_crt_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs7_set_crt_raw, (gnutls_pkcs7_t a0, const gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pkcs7_sign, (gnutls_pkcs7_t a0, gnutls_x509_crt_t a1, gnutls_privkey_t a2, const gnutls_datum_t * a3, gnutls_pkcs7_attrs_t a4, gnutls_pkcs7_attrs_t a5, gnutls_digest_algorithm_t a6, unsigned int a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_VOID(gnutls_pkcs7_signature_info_deinit, (gnutls_pkcs7_signature_info_st * a0), (a0))
WRAP_RET(int, gnutls_pkcs7_verify, (gnutls_pkcs7_t a0, gnutls_x509_trust_list_t a1, gnutls_typed_vdata_st * a2, unsigned int a3, unsigned int a4, const gnutls_datum_t * a5, unsigned int a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_pkcs7_verify_direct, (gnutls_pkcs7_t a0, gnutls_x509_crt_t a1, unsigned int a2, const gnutls_datum_t * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pkcs8_info, (const gnutls_datum_t * a0, gnutls_x509_crt_fmt_t a1, unsigned int * a2, unsigned int * a3, void * a4, unsigned int * a5, unsigned int * a6, char ** a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(const char *, gnutls_pkcs_schema_get_name, (unsigned int a0), (a0), NULL)
WRAP_RET(const char *, gnutls_pkcs_schema_get_oid, (unsigned int a0), (a0), NULL)
WRAP_RET(int, gnutls_prf, (gnutls_session_t a0, size_t a1, const char * a2, int a3, size_t a4, const char * a5, size_t a6, char * a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(int, gnutls_prf_early, (gnutls_session_t a0, size_t a1, const char * a2, size_t a3, const char * a4, size_t a5, char * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(gnutls_digest_algorithm_t, gnutls_prf_hash_get, (const gnutls_session_t a0), (a0), ((gnutls_digest_algorithm_t)0))
WRAP_RET(int, gnutls_prf_raw, (gnutls_session_t a0, size_t a1, const char * a2, size_t a3, const char * a4, size_t a5, char * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_prf_rfc5705, (gnutls_session_t a0, size_t a1, const char * a2, size_t a3, const char * a4, size_t a5, char * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_priority_certificate_type_list, (gnutls_priority_t a0, const unsigned int ** a1), (a0, a1), -1)
WRAP_RET(int, gnutls_priority_certificate_type_list2, (gnutls_priority_t a0, const unsigned int ** a1, gnutls_ctype_target_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_priority_cipher_list, (gnutls_priority_t a0, const unsigned int ** a1), (a0, a1), -1)
WRAP_RET(int, gnutls_priority_compression_list, (gnutls_priority_t a0, const unsigned int ** a1), (a0, a1), -1)
WRAP_VOID(gnutls_priority_deinit, (gnutls_priority_t a0), (a0))
WRAP_RET(int, gnutls_priority_ecc_curve_list, (gnutls_priority_t a0, const unsigned int ** a1), (a0, a1), -1)
WRAP_RET(int, gnutls_priority_get_cipher_suite_index, (gnutls_priority_t a0, unsigned int a1, unsigned int * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_priority_group_list, (gnutls_priority_t a0, const unsigned int ** a1), (a0, a1), -1)
WRAP_RET(int, gnutls_priority_init, (gnutls_priority_t * a0, const char * a1, const char ** a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_priority_init2, (gnutls_priority_t * a0, const char * a1, const char ** a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_priority_kx_list, (gnutls_priority_t a0, const unsigned int ** a1), (a0, a1), -1)
WRAP_RET(int, gnutls_priority_mac_list, (gnutls_priority_t a0, const unsigned int ** a1), (a0, a1), -1)
WRAP_RET(int, gnutls_priority_protocol_list, (gnutls_priority_t a0, const unsigned int ** a1), (a0, a1), -1)
WRAP_RET(int, gnutls_priority_set, (gnutls_session_t a0, gnutls_priority_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_priority_set_direct, (gnutls_session_t a0, const char * a1, const char ** a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_priority_sign_list, (gnutls_priority_t a0, const unsigned int ** a1), (a0, a1), -1)
WRAP_RET(const char *, gnutls_priority_string_list, (unsigned int a0, unsigned int a1), (a0, a1), NULL)
WRAP_RET(int, gnutls_privkey_decrypt_data, (gnutls_privkey_t a0, unsigned int a1, const gnutls_datum_t * a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_privkey_decrypt_data2, (gnutls_privkey_t a0, unsigned int a1, const gnutls_datum_t * a2, unsigned char * a3, size_t a4), (a0, a1, a2, a3, a4), -1)
WRAP_VOID(gnutls_privkey_deinit, (gnutls_privkey_t a0), (a0))
WRAP_RET(int, gnutls_privkey_derive_secret, (gnutls_privkey_t a0, gnutls_pubkey_t a1, const gnutls_datum_t * a2, gnutls_datum_t * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_privkey_export_dh_raw, (gnutls_privkey_t a0, gnutls_dh_params_t a1, gnutls_datum_t * a2, gnutls_datum_t * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_privkey_export_dsa_raw, (gnutls_privkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_privkey_export_dsa_raw2, (gnutls_privkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, unsigned int a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_privkey_export_ecc_raw, (gnutls_privkey_t a0, gnutls_ecc_curve_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_privkey_export_ecc_raw2, (gnutls_privkey_t a0, gnutls_ecc_curve_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_privkey_export_gost_raw2, (gnutls_privkey_t a0, gnutls_ecc_curve_t * a1, gnutls_digest_algorithm_t * a2, gnutls_gost_paramset_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, gnutls_datum_t * a6, unsigned int a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(int, gnutls_privkey_export_openpgp, (gnutls_privkey_t a0, gnutls_openpgp_privkey_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_privkey_export_pkcs11, (gnutls_privkey_t a0, gnutls_pkcs11_privkey_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_privkey_export_rsa_raw, (gnutls_privkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, gnutls_datum_t * a6, gnutls_datum_t * a7, gnutls_datum_t * a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_RET(int, gnutls_privkey_export_rsa_raw2, (gnutls_privkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, gnutls_datum_t * a6, gnutls_datum_t * a7, gnutls_datum_t * a8, unsigned int a9), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9), -1)
WRAP_RET(int, gnutls_privkey_export_x509, (gnutls_privkey_t a0, gnutls_x509_privkey_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_privkey_generate, (gnutls_privkey_t a0, gnutls_pk_algorithm_t a1, unsigned int a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_privkey_generate2, (gnutls_privkey_t a0, gnutls_pk_algorithm_t a1, unsigned int a2, unsigned int a3, const gnutls_keygen_data_st * a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_privkey_get_pk_algorithm, (gnutls_privkey_t a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_privkey_get_seed, (gnutls_privkey_t a0, gnutls_digest_algorithm_t * a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_privkey_get_spki, (gnutls_privkey_t a0, const gnutls_x509_spki_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(gnutls_privkey_type_t, gnutls_privkey_get_type, (gnutls_privkey_t a0), (a0), ((gnutls_privkey_type_t)0))
WRAP_RET(int, gnutls_privkey_import_dh_raw, (gnutls_privkey_t a0, const gnutls_dh_params_t a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_privkey_import_dsa_raw, (gnutls_privkey_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4, const gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_privkey_import_ecc_raw, (gnutls_privkey_t a0, gnutls_ecc_curve_t a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_privkey_import_ext, (gnutls_privkey_t a0, gnutls_pk_algorithm_t a1, void * a2, gnutls_privkey_sign_func a3, gnutls_privkey_decrypt_func a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_privkey_import_ext2, (gnutls_privkey_t a0, gnutls_pk_algorithm_t a1, void * a2, gnutls_privkey_sign_func a3, gnutls_privkey_decrypt_func a4, gnutls_privkey_deinit_func a5, unsigned int a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_privkey_import_ext3, (gnutls_privkey_t a0, void * a1, gnutls_privkey_sign_func a2, gnutls_privkey_decrypt_func a3, gnutls_privkey_deinit_func a4, gnutls_privkey_info_func a5, unsigned int a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_privkey_import_ext4, (gnutls_privkey_t a0, void * a1, gnutls_privkey_sign_data_func a2, gnutls_privkey_sign_hash_func a3, gnutls_privkey_decrypt_func a4, gnutls_privkey_deinit_func a5, gnutls_privkey_info_func a6, unsigned int a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(int, gnutls_privkey_import_gost_raw, (gnutls_privkey_t a0, gnutls_ecc_curve_t a1, gnutls_digest_algorithm_t a2, gnutls_gost_paramset_t a3, const gnutls_datum_t * a4, const gnutls_datum_t * a5, const gnutls_datum_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_privkey_import_openpgp, (gnutls_privkey_t a0, gnutls_openpgp_privkey_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_privkey_import_openpgp_raw, (gnutls_privkey_t a0, const gnutls_datum_t * a1, gnutls_openpgp_crt_fmt_t a2, const unsigned char * a3, const char * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_privkey_import_pkcs11, (gnutls_privkey_t a0, gnutls_pkcs11_privkey_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_privkey_import_rsa_raw, (gnutls_privkey_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4, const gnutls_datum_t * a5, const gnutls_datum_t * a6, const gnutls_datum_t * a7, const gnutls_datum_t * a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_RET(int, gnutls_privkey_import_tpm_raw, (gnutls_privkey_t a0, const gnutls_datum_t * a1, gnutls_tpmkey_fmt_t a2, const char * a3, const char * a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_privkey_import_tpm_url, (gnutls_privkey_t a0, const char * a1, const char * a2, const char * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_privkey_import_url, (gnutls_privkey_t a0, const char * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_privkey_import_x509, (gnutls_privkey_t a0, gnutls_x509_privkey_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_privkey_import_x509_raw, (gnutls_privkey_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2, const char * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_privkey_init, (gnutls_privkey_t * a0), (a0), -1)
WRAP_VOID(gnutls_privkey_set_flags, (gnutls_privkey_t a0, unsigned int a1), (a0, a1))
WRAP_VOID(gnutls_privkey_set_pin_function, (gnutls_privkey_t a0, gnutls_pin_callback_t a1, void * a2), (a0, a1, a2))
WRAP_RET(int, gnutls_privkey_set_spki, (gnutls_privkey_t a0, const gnutls_x509_spki_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_privkey_sign_data, (gnutls_privkey_t a0, gnutls_digest_algorithm_t a1, unsigned int a2, const gnutls_datum_t * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_privkey_sign_data2, (gnutls_privkey_t a0, gnutls_sign_algorithm_t a1, unsigned int a2, const gnutls_datum_t * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_privkey_sign_hash, (gnutls_privkey_t a0, gnutls_digest_algorithm_t a1, unsigned int a2, const gnutls_datum_t * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_privkey_sign_hash2, (gnutls_privkey_t a0, gnutls_sign_algorithm_t a1, unsigned int a2, const gnutls_datum_t * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_privkey_status, (gnutls_privkey_t a0), (a0), -1)
WRAP_RET(int, gnutls_privkey_verify_params, (gnutls_privkey_t a0), (a0), -1)
WRAP_RET(int, gnutls_privkey_verify_seed, (gnutls_privkey_t a0, gnutls_digest_algorithm_t a1, const void * a2, size_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(gnutls_protocol_t, gnutls_protocol_get_id, (const char * a0), (a0), ((gnutls_protocol_t)0))
WRAP_RET(const char *, gnutls_protocol_get_name, (gnutls_protocol_t a0), (a0), NULL)
WRAP_RET(gnutls_protocol_t, gnutls_protocol_get_version, (gnutls_session_t a0), (a0), ((gnutls_protocol_t)0))
WRAP_RET(const gnutls_protocol_t *, gnutls_protocol_list, (), (), NULL)
WRAP_RET(int, gnutls_protocol_set_enabled, (gnutls_protocol_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_psk_allocate_client_credentials, (gnutls_psk_client_credentials_t * a0), (a0), -1)
WRAP_RET(int, gnutls_psk_allocate_client_credentials2, (gnutls_psk_client_credentials_t * a0, gnutls_mac_algorithm_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_psk_allocate_server_credentials, (gnutls_psk_server_credentials_t * a0), (a0), -1)
WRAP_RET(int, gnutls_psk_allocate_server_credentials2, (gnutls_psk_server_credentials_t * a0, gnutls_mac_algorithm_t a1), (a0, a1), -1)
WRAP_RET(const char *, gnutls_psk_client_get_hint, (gnutls_session_t a0), (a0), NULL)
WRAP_RET(int, gnutls_psk_format_imported_identity, (const gnutls_datum_t * a0, const gnutls_datum_t * a1, gnutls_protocol_t a2, gnutls_digest_algorithm_t a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_VOID(gnutls_psk_free_client_credentials, (gnutls_psk_client_credentials_t a0), (a0))
WRAP_VOID(gnutls_psk_free_server_credentials, (gnutls_psk_server_credentials_t a0), (a0))
WRAP_RET(const char *, gnutls_psk_server_get_username, (gnutls_session_t a0), (a0), NULL)
WRAP_RET(int, gnutls_psk_server_get_username2, (gnutls_session_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_psk_set_client_credentials, (gnutls_psk_client_credentials_t a0, const char * a1, const gnutls_datum_t * a2, gnutls_psk_key_flags a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_psk_set_client_credentials2, (gnutls_psk_client_credentials_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, gnutls_psk_key_flags a3), (a0, a1, a2, a3), -1)
WRAP_VOID(gnutls_psk_set_client_credentials_function, (gnutls_psk_client_credentials_t a0, gnutls_psk_client_credentials_function (*a1)), (a0, a1))
WRAP_VOID(gnutls_psk_set_client_credentials_function2, (gnutls_psk_client_credentials_t a0, gnutls_psk_client_credentials_function2 (*a1)), (a0, a1))
WRAP_VOID(gnutls_psk_set_client_credentials_function3, (gnutls_psk_client_credentials_t a0, gnutls_psk_client_credentials_function3 (*a1)), (a0, a1))
WRAP_VOID(gnutls_psk_set_params_function, (gnutls_psk_server_credentials_t a0, gnutls_params_function (*a1)), (a0, a1))
WRAP_RET(int, gnutls_psk_set_server_credentials_file, (gnutls_psk_server_credentials_t a0, const char * a1), (a0, a1), -1)
WRAP_VOID(gnutls_psk_set_server_credentials_function, (gnutls_psk_server_credentials_t a0, gnutls_psk_server_credentials_function (*a1)), (a0, a1))
WRAP_VOID(gnutls_psk_set_server_credentials_function2, (gnutls_psk_server_credentials_t a0, gnutls_psk_server_credentials_function2 (*a1)), (a0, a1))
WRAP_VOID(gnutls_psk_set_server_credentials_function3, (gnutls_psk_server_credentials_t a0, gnutls_psk_server_credentials_function3 (*a1)), (a0, a1))
WRAP_RET(int, gnutls_psk_set_server_credentials_hint, (gnutls_psk_server_credentials_t a0, const char * a1), (a0, a1), -1)
WRAP_VOID(gnutls_psk_set_server_dh_params, (gnutls_psk_server_credentials_t a0, gnutls_dh_params_t a1), (a0, a1))
WRAP_RET(int, gnutls_psk_set_server_known_dh_params, (gnutls_psk_server_credentials_t a0, gnutls_sec_param_t a1), (a0, a1), -1)
WRAP_VOID(gnutls_psk_set_server_params_function, (gnutls_psk_server_credentials_t a0, gnutls_params_function (*a1)), (a0, a1))
WRAP_VOID(gnutls_pubkey_deinit, (gnutls_pubkey_t a0), (a0))
WRAP_RET(int, gnutls_pubkey_encrypt_data, (gnutls_pubkey_t a0, unsigned int a1, const gnutls_datum_t * a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pubkey_export, (gnutls_pubkey_t a0, gnutls_x509_crt_fmt_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pubkey_export2, (gnutls_pubkey_t a0, gnutls_x509_crt_fmt_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_export_dh_raw, (gnutls_pubkey_t a0, gnutls_dh_params_t a1, gnutls_datum_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pubkey_export_dsa_raw, (gnutls_pubkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pubkey_export_dsa_raw2, (gnutls_pubkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_pubkey_export_ecc_raw, (gnutls_pubkey_t a0, gnutls_ecc_curve_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pubkey_export_ecc_raw2, (gnutls_pubkey_t a0, gnutls_ecc_curve_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pubkey_export_ecc_x962, (gnutls_pubkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_export_gost_raw2, (gnutls_pubkey_t a0, gnutls_ecc_curve_t * a1, gnutls_digest_algorithm_t * a2, gnutls_gost_paramset_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, unsigned int a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_pubkey_export_mldsa_raw, (gnutls_pubkey_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pubkey_export_rsa_raw, (gnutls_pubkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_export_rsa_raw2, (gnutls_pubkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pubkey_get_key_id, (gnutls_pubkey_t a0, unsigned int a1, unsigned char * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pubkey_get_key_usage, (gnutls_pubkey_t a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pubkey_get_openpgp_key_id, (gnutls_pubkey_t a0, unsigned int a1, unsigned char * a2, size_t * a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pubkey_get_pk_algorithm, (gnutls_pubkey_t a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_pubkey_get_preferred_hash_algorithm, (gnutls_pubkey_t a0, gnutls_digest_algorithm_t * a1, unsigned int * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_get_spki, (gnutls_pubkey_t a0, const gnutls_x509_spki_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_import, (gnutls_pubkey_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_import_dh_raw, (gnutls_pubkey_t a0, const gnutls_dh_params_t a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_import_dsa_raw, (gnutls_pubkey_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pubkey_import_ecc_raw, (gnutls_pubkey_t a0, gnutls_ecc_curve_t a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pubkey_import_ecc_x962, (gnutls_pubkey_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_import_gost_raw, (gnutls_pubkey_t a0, gnutls_ecc_curve_t a1, gnutls_digest_algorithm_t a2, gnutls_gost_paramset_t a3, const gnutls_datum_t * a4, const gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_pubkey_import_mldsa_raw, (gnutls_pubkey_t a0, gnutls_pk_algorithm_t a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_import_openpgp, (gnutls_pubkey_t a0, gnutls_openpgp_crt_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_import_openpgp_raw, (gnutls_pubkey_t a0, const gnutls_datum_t * a1, gnutls_openpgp_crt_fmt_t a2, const unsigned char * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pubkey_import_pkcs11, (gnutls_pubkey_t a0, gnutls_pkcs11_obj_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_import_privkey, (gnutls_pubkey_t a0, gnutls_privkey_t a1, unsigned int a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pubkey_import_rsa_raw, (gnutls_pubkey_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_import_tpm_raw, (gnutls_pubkey_t a0, const gnutls_datum_t * a1, gnutls_tpmkey_fmt_t a2, const char * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pubkey_import_tpm_url, (gnutls_pubkey_t a0, const char * a1, const char * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pubkey_import_url, (gnutls_pubkey_t a0, const char * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_import_x509, (gnutls_pubkey_t a0, gnutls_x509_crt_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_import_x509_crq, (gnutls_pubkey_t a0, gnutls_x509_crq_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_import_x509_raw, (gnutls_pubkey_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_pubkey_init, (gnutls_pubkey_t * a0), (a0), -1)
WRAP_RET(int, gnutls_pubkey_print, (gnutls_pubkey_t a0, gnutls_certificate_print_formats_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_set_key_usage, (gnutls_pubkey_t a0, unsigned int a1), (a0, a1), -1)
WRAP_VOID(gnutls_pubkey_set_pin_function, (gnutls_pubkey_t a0, gnutls_pin_callback_t a1, void * a2), (a0, a1, a2))
WRAP_RET(int, gnutls_pubkey_set_spki, (gnutls_pubkey_t a0, const gnutls_x509_spki_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_pubkey_verify_data2, (gnutls_pubkey_t a0, gnutls_sign_algorithm_t a1, unsigned int a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pubkey_verify_hash2, (gnutls_pubkey_t a0, gnutls_sign_algorithm_t a1, unsigned int a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_pubkey_verify_params, (gnutls_pubkey_t a0), (a0), -1)
WRAP_RET(int, gnutls_random_art, (gnutls_random_art_t a0, const char * a1, unsigned int a2, void * a3, size_t a4, gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_range_split, (gnutls_session_t a0, const gnutls_range_st * a1, gnutls_range_st * a2, gnutls_range_st * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_reauth, (gnutls_session_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(unsigned int, gnutls_record_can_use_length_hiding, (gnutls_session_t a0), (a0), 0)
WRAP_RET(size_t, gnutls_record_check_corked, (gnutls_session_t a0), (a0), 0)
WRAP_RET(size_t, gnutls_record_check_pending, (gnutls_session_t a0), (a0), 0)
WRAP_VOID(gnutls_record_cork, (gnutls_session_t a0), (a0))
WRAP_VOID(gnutls_record_disable_padding, (gnutls_session_t a0), (a0))
WRAP_RET(size_t, gnutls_record_discard_queued, (gnutls_session_t a0), (a0), 0)
WRAP_RET(int, gnutls_record_get_direction, (gnutls_session_t a0), (a0), -1)
WRAP_RET(unsigned int, gnutls_record_get_discarded, (gnutls_session_t a0), (a0), 0)
WRAP_RET(size_t, gnutls_record_get_max_early_data_size, (gnutls_session_t a0), (a0), 0)
WRAP_RET(size_t, gnutls_record_get_max_send_size, (gnutls_session_t a0), (a0), 0)
WRAP_RET(size_t, gnutls_record_get_max_size, (gnutls_session_t a0), (a0), 0)
WRAP_RET(int, gnutls_record_get_state, (gnutls_session_t a0, unsigned int a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, unsigned char * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(size_t, gnutls_record_overhead_size, (gnutls_session_t a0), (a0), 0)
WRAP_RET(ssize_t, gnutls_record_recv, (gnutls_session_t a0, void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(ssize_t, gnutls_record_recv_early_data, (gnutls_session_t a0, void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(ssize_t, gnutls_record_recv_packet, (gnutls_session_t a0, gnutls_packet_t * a1), (a0, a1), -1)
WRAP_RET(ssize_t, gnutls_record_recv_seq, (gnutls_session_t a0, void * a1, size_t a2, unsigned char * a3), (a0, a1, a2, a3), -1)
WRAP_RET(ssize_t, gnutls_record_send, (gnutls_session_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(ssize_t, gnutls_record_send2, (gnutls_session_t a0, const void * a1, size_t a2, size_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(ssize_t, gnutls_record_send_early_data, (gnutls_session_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(ssize_t, gnutls_record_send_file, (gnutls_session_t a0, int a1, off_t * a2, size_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(ssize_t, gnutls_record_send_range, (gnutls_session_t a0, const void * a1, size_t a2, const gnutls_range_st * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_record_set_max_early_data_size, (gnutls_session_t a0, size_t a1), (a0, a1), -1)
WRAP_RET(ssize_t, gnutls_record_set_max_recv_size, (gnutls_session_t a0, size_t a1), (a0, a1), -1)
WRAP_RET(ssize_t, gnutls_record_set_max_size, (gnutls_session_t a0, size_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_record_set_state, (gnutls_session_t a0, unsigned int a1, const unsigned char * a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_record_set_timeout, (gnutls_session_t a0, unsigned int a1), (a0, a1))
WRAP_RET(int, gnutls_record_uncork, (gnutls_session_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_register_custom_url, (const gnutls_custom_url_st * a0), (a0), -1)
WRAP_RET(int, gnutls_rehandshake, (gnutls_session_t a0), (a0), -1)
WRAP_RET(int, gnutls_rnd, (gnutls_rnd_level_t a0, void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_rnd_refresh, (), ())
WRAP_RET(unsigned int, gnutls_safe_renegotiation_status, (gnutls_session_t a0), (a0), 0)
WRAP_RET(const char *, gnutls_sec_param_get_name, (gnutls_sec_param_t a0), (a0), NULL)
WRAP_RET(unsigned int, gnutls_sec_param_to_pk_bits, (gnutls_pk_algorithm_t a0, gnutls_sec_param_t a1), (a0, a1), 0)
WRAP_RET(unsigned int, gnutls_sec_param_to_symmetric_bits, (gnutls_sec_param_t a0), (a0), 0)
WRAP_RET(int, gnutls_server_name_get, (gnutls_session_t a0, void * a1, size_t * a2, unsigned int * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_server_name_set, (gnutls_session_t a0, gnutls_server_name_type_t a1, const void * a2, size_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_session_channel_binding, (gnutls_session_t a0, gnutls_channel_binding_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_session_enable_compatibility_mode, (gnutls_session_t a0), (a0))
WRAP_RET(unsigned int, gnutls_session_etm_status, (gnutls_session_t a0), (a0), 0)
WRAP_RET(unsigned int, gnutls_session_ext_master_secret_status, (gnutls_session_t a0), (a0), 0)
WRAP_RET(int, gnutls_session_ext_register, (gnutls_session_t a0, const char * a1, int a2, gnutls_ext_parse_type_t a3, gnutls_ext_recv_func a4, gnutls_ext_send_func a5, gnutls_ext_deinit_data_func a6, gnutls_ext_pack_func a7, gnutls_ext_unpack_func a8, unsigned int a9), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9), -1)
WRAP_VOID(gnutls_session_force_valid, (gnutls_session_t a0), (a0))
WRAP_RET(int, gnutls_session_get_data, (gnutls_session_t a0, void * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_session_get_data2, (gnutls_session_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(char *, gnutls_session_get_desc, (gnutls_session_t a0), (a0), NULL)
WRAP_RET(unsigned int, gnutls_session_get_flags, (gnutls_session_t a0), (a0), 0)
WRAP_RET(int, gnutls_session_get_id, (gnutls_session_t a0, void * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_session_get_id2, (gnutls_session_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(gnutls_keylog_func, gnutls_session_get_keylog_function, (const gnutls_session_t a0), (a0), ((gnutls_keylog_func)0))
WRAP_VOID(gnutls_session_get_master_secret, (gnutls_session_t a0, gnutls_datum_t * a1), (a0, a1))
WRAP_RET(void *, gnutls_session_get_ptr, (gnutls_session_t a0), (a0), NULL)
WRAP_VOID(gnutls_session_get_random, (gnutls_session_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2))
WRAP_RET(unsigned int, gnutls_session_get_verify_cert_status, (gnutls_session_t a0), (a0), 0)
WRAP_RET(int, gnutls_session_is_resumed, (gnutls_session_t a0), (a0), -1)
WRAP_RET(int, gnutls_session_key_update, (gnutls_session_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_session_resumption_requested, (gnutls_session_t a0), (a0), -1)
WRAP_RET(int, gnutls_session_set_data, (gnutls_session_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_session_set_id, (gnutls_session_t a0, const gnutls_datum_t * a1), (a0, a1), -1)
WRAP_VOID(gnutls_session_set_keylog_function, (gnutls_session_t a0, gnutls_keylog_func a1), (a0, a1))
WRAP_RET(int, gnutls_session_set_premaster, (gnutls_session_t a0, unsigned int a1, gnutls_protocol_t a2, gnutls_kx_algorithm_t a3, gnutls_cipher_algorithm_t a4, gnutls_mac_algorithm_t a5, gnutls_compression_method_t a6, const gnutls_datum_t * a7, const gnutls_datum_t * a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_VOID(gnutls_session_set_ptr, (gnutls_session_t a0, void * a1), (a0, a1))
WRAP_VOID(gnutls_session_set_verify_cert, (gnutls_session_t a0, const char * a1, unsigned int a2), (a0, a1, a2))
WRAP_VOID(gnutls_session_set_verify_cert2, (gnutls_session_t a0, gnutls_typed_vdata_st * a1, unsigned int a2, unsigned int a3), (a0, a1, a2, a3))
WRAP_VOID(gnutls_session_set_verify_function, (gnutls_session_t a0, gnutls_certificate_verify_function (*a1)), (a0, a1))
WRAP_VOID(gnutls_session_set_verify_output_function, (gnutls_session_t a0, gnutls_verify_output_function (*a1)), (a0, a1))
WRAP_RET(int, gnutls_session_supplemental_register, (gnutls_session_t a0, const char * a1, gnutls_supplemental_data_format_type_t a2, gnutls_supp_recv_func a3, gnutls_supp_send_func a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_session_ticket_enable_client, (gnutls_session_t a0), (a0), -1)
WRAP_RET(int, gnutls_session_ticket_enable_server, (gnutls_session_t a0, const gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_session_ticket_key_generate, (gnutls_datum_t * a0), (a0), -1)
WRAP_RET(int, gnutls_session_ticket_send, (gnutls_session_t a0, unsigned int a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_set_default_priority, (gnutls_session_t a0), (a0), -1)
WRAP_RET(int, gnutls_set_default_priority_append, (gnutls_session_t a0, const char * a1, const char ** a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_sign_algorithm_get, (gnutls_session_t a0), (a0), -1)
WRAP_RET(int, gnutls_sign_algorithm_get_client, (gnutls_session_t a0), (a0), -1)
WRAP_RET(int, gnutls_sign_algorithm_get_requested, (gnutls_session_t a0, size_t a1, gnutls_sign_algorithm_t * a2), (a0, a1, a2), -1)
WRAP_RET(gnutls_digest_algorithm_t, gnutls_sign_get_hash_algorithm, (gnutls_sign_algorithm_t a0), (a0), ((gnutls_digest_algorithm_t)0))
WRAP_RET(gnutls_sign_algorithm_t, gnutls_sign_get_id, (const char * a0), (a0), ((gnutls_sign_algorithm_t)0))
WRAP_RET(const char *, gnutls_sign_get_name, (gnutls_sign_algorithm_t a0), (a0), NULL)
WRAP_RET(const char *, gnutls_sign_get_oid, (gnutls_sign_algorithm_t a0), (a0), NULL)
WRAP_RET(gnutls_pk_algorithm_t, gnutls_sign_get_pk_algorithm, (gnutls_sign_algorithm_t a0), (a0), ((gnutls_pk_algorithm_t)0))
WRAP_RET(unsigned int, gnutls_sign_is_secure, (gnutls_sign_algorithm_t a0), (a0), 0)
WRAP_RET(unsigned int, gnutls_sign_is_secure2, (gnutls_sign_algorithm_t a0, unsigned int a1), (a0, a1), 0)
WRAP_RET(const gnutls_sign_algorithm_t *, gnutls_sign_list, (), (), NULL)
WRAP_RET(int, gnutls_sign_set_secure, (gnutls_sign_algorithm_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_sign_set_secure_for_certs, (gnutls_sign_algorithm_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(unsigned int, gnutls_sign_supports_pk_algorithm, (gnutls_sign_algorithm_t a0, gnutls_pk_algorithm_t a1), (a0, a1), 0)
WRAP_RET(int, gnutls_srp_allocate_client_credentials, (gnutls_srp_client_credentials_t * a0), (a0), -1)
WRAP_RET(int, gnutls_srp_allocate_server_credentials, (gnutls_srp_server_credentials_t * a0), (a0), -1)
WRAP_RET(int, gnutls_srp_base64_decode, (const gnutls_datum_t * a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_srp_base64_decode2, (const gnutls_datum_t * a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_srp_base64_encode, (const gnutls_datum_t * a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_srp_base64_encode2, (const gnutls_datum_t * a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_VOID(gnutls_srp_free_client_credentials, (gnutls_srp_client_credentials_t a0), (a0))
WRAP_VOID(gnutls_srp_free_server_credentials, (gnutls_srp_server_credentials_t a0), (a0))
WRAP_RET(const char *, gnutls_srp_server_get_username, (gnutls_session_t a0), (a0), NULL)
WRAP_RET(int, gnutls_srp_set_client_credentials, (gnutls_srp_client_credentials_t a0, const char * a1, const char * a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_srp_set_client_credentials_function, (gnutls_srp_client_credentials_t a0, gnutls_srp_client_credentials_function (*a1)), (a0, a1))
WRAP_VOID(gnutls_srp_set_prime_bits, (gnutls_session_t a0, unsigned int a1), (a0, a1))
WRAP_RET(int, gnutls_srp_set_server_credentials_file, (gnutls_srp_server_credentials_t a0, const char * a1, const char * a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_srp_set_server_credentials_function, (gnutls_srp_server_credentials_t a0, gnutls_srp_server_credentials_function (*a1)), (a0, a1))
WRAP_VOID(gnutls_srp_set_server_fake_salt_seed, (gnutls_srp_server_credentials_t a0, const gnutls_datum_t * a1, unsigned int a2), (a0, a1, a2))
WRAP_RET(int, gnutls_srp_verifier, (const char * a0, const char * a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4, gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_srtp_get_keys, (gnutls_session_t a0, void * a1, unsigned int a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, gnutls_datum_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_srtp_get_mki, (gnutls_session_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_srtp_get_profile_id, (const char * a0, gnutls_srtp_profile_t * a1), (a0, a1), -1)
WRAP_RET(const char *, gnutls_srtp_get_profile_name, (gnutls_srtp_profile_t a0), (a0), NULL)
WRAP_RET(int, gnutls_srtp_get_selected_profile, (gnutls_session_t a0, gnutls_srtp_profile_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_srtp_set_mki, (gnutls_session_t a0, const gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_srtp_set_profile, (gnutls_session_t a0, gnutls_srtp_profile_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_srtp_set_profile_direct, (gnutls_session_t a0, const char * a1, const char ** a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_store_commitment, (const char * a0, gnutls_tdb_t a1, const char * a2, const char * a3, gnutls_digest_algorithm_t a4, const gnutls_datum_t * a5, time_t a6, unsigned int a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(int, gnutls_store_pubkey, (const char * a0, gnutls_tdb_t a1, const char * a2, const char * a3, gnutls_certificate_type_t a4, const gnutls_datum_t * a5, time_t a6, unsigned int a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(const char *, gnutls_strerror, (int a0), (a0), NULL)
WRAP_RET(const char *, gnutls_strerror_name, (int a0), (a0), NULL)
WRAP_VOID(gnutls_subject_alt_names_deinit, (gnutls_subject_alt_names_t a0), (a0))
WRAP_RET(int, gnutls_subject_alt_names_get, (gnutls_subject_alt_names_t a0, unsigned int a1, unsigned int * a2, gnutls_datum_t * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_subject_alt_names_init, (gnutls_subject_alt_names_t * a0), (a0), -1)
WRAP_RET(int, gnutls_subject_alt_names_set, (gnutls_subject_alt_names_t a0, unsigned int a1, const gnutls_datum_t * a2, const char * a3), (a0, a1, a2, a3), -1)
WRAP_RET(const char *, gnutls_supplemental_get_name, (gnutls_supplemental_data_format_type_t a0), (a0), NULL)
WRAP_VOID(gnutls_supplemental_recv, (gnutls_session_t a0, unsigned int a1), (a0, a1))
WRAP_RET(int, gnutls_supplemental_register, (const char * a0, gnutls_supplemental_data_format_type_t a1, gnutls_supp_recv_func a2, gnutls_supp_send_func a3), (a0, a1, a2, a3), -1)
WRAP_VOID(gnutls_supplemental_send, (gnutls_session_t a0, unsigned int a1), (a0, a1))
WRAP_RET(int, gnutls_system_key_add_x509, (gnutls_x509_crt_t a0, gnutls_x509_privkey_t a1, const char * a2, char ** a3, char ** a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_system_key_delete, (const char * a0, const char * a1), (a0, a1), -1)
WRAP_VOID(gnutls_system_key_iter_deinit, (gnutls_system_key_iter_t a0), (a0))
WRAP_RET(int, gnutls_system_key_iter_get_info, (gnutls_system_key_iter_t * a0, unsigned int a1, char ** a2, char ** a3, char ** a4, gnutls_datum_t * a5, unsigned int a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_system_recv_timeout, (gnutls_transport_ptr_t a0, unsigned int a1), (a0, a1), -1)
WRAP_VOID(gnutls_tdb_deinit, (gnutls_tdb_t a0), (a0))
WRAP_RET(int, gnutls_tdb_init, (gnutls_tdb_t * a0), (a0), -1)
WRAP_VOID(gnutls_tdb_set_store_commitment_func, (gnutls_tdb_t a0, gnutls_tdb_store_commitment_func a1), (a0, a1))
WRAP_VOID(gnutls_tdb_set_store_func, (gnutls_tdb_t a0, gnutls_tdb_store_func a1), (a0, a1))
WRAP_VOID(gnutls_tdb_set_verify_func, (gnutls_tdb_t a0, gnutls_tdb_verify_func a1), (a0, a1))
WRAP_RET(int, gnutls_tlsprf_self_test, (unsigned int a0, gnutls_mac_algorithm_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_tpm_get_registered, (gnutls_tpm_key_list_t * a0), (a0), -1)
WRAP_VOID(gnutls_tpm_key_list_deinit, (gnutls_tpm_key_list_t a0), (a0))
WRAP_RET(int, gnutls_tpm_key_list_get_url, (gnutls_tpm_key_list_t a0, unsigned int a1, char ** a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_tpm_privkey_delete, (const char * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_tpm_privkey_generate, (gnutls_pk_algorithm_t a0, unsigned int a1, const char * a2, const char * a3, gnutls_tpmkey_fmt_t a4, gnutls_x509_crt_fmt_t a5, gnutls_datum_t * a6, gnutls_datum_t * a7, unsigned int a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_RET(int, gnutls_transport_get_int, (gnutls_session_t a0), (a0), -1)
WRAP_VOID(gnutls_transport_get_int2, (gnutls_session_t a0, int * a1, int * a2), (a0, a1, a2))
WRAP_RET(gnutls_transport_ptr_t, gnutls_transport_get_ptr, (gnutls_session_t a0), (a0), ((gnutls_transport_ptr_t)0))
WRAP_VOID(gnutls_transport_get_ptr2, (gnutls_session_t a0, gnutls_transport_ptr_t * a1, gnutls_transport_ptr_t * a2), (a0, a1, a2))
WRAP_RET(gnutls_transport_ktls_enable_flags_t, gnutls_transport_is_ktls_enabled, (gnutls_session_t a0), (a0), ((gnutls_transport_ktls_enable_flags_t)0))
WRAP_VOID(gnutls_transport_set_errno, (gnutls_session_t a0, int a1), (a0, a1))
WRAP_VOID(gnutls_transport_set_errno_function, (gnutls_session_t a0, gnutls_errno_func a1), (a0, a1))
WRAP_VOID(gnutls_transport_set_fastopen, (gnutls_session_t a0, int a1, struct sockaddr * a2, socklen_t a3, unsigned int a4), (a0, a1, a2, a3, a4))
WRAP_VOID(gnutls_transport_set_int2, (gnutls_session_t a0, int a1, int a2), (a0, a1, a2))
WRAP_VOID(gnutls_transport_set_ptr, (gnutls_session_t a0, gnutls_transport_ptr_t a1), (a0, a1))
WRAP_VOID(gnutls_transport_set_ptr2, (gnutls_session_t a0, gnutls_transport_ptr_t a1, gnutls_transport_ptr_t a2), (a0, a1, a2))
WRAP_VOID(gnutls_transport_set_pull_function, (gnutls_session_t a0, gnutls_pull_func a1), (a0, a1))
WRAP_VOID(gnutls_transport_set_pull_timeout_function, (gnutls_session_t a0, gnutls_pull_timeout_func a1), (a0, a1))
WRAP_VOID(gnutls_transport_set_push_function, (gnutls_session_t a0, gnutls_push_func a1), (a0, a1))
WRAP_VOID(gnutls_transport_set_vec_push_function, (gnutls_session_t a0, gnutls_vec_push_func a1), (a0, a1))
WRAP_RET(unsigned int, gnutls_url_is_supported, (const char * a0), (a0), 0)
WRAP_RET(int, gnutls_utf8_password_normalize, (const unsigned char * a0, unsigned int a1, gnutls_datum_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_verify_stored_pubkey, (const char * a0, gnutls_tdb_t a1, const char * a2, const char * a3, gnutls_certificate_type_t a4, const gnutls_datum_t * a5, unsigned int a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_VOID(gnutls_x509_aia_deinit, (gnutls_x509_aia_t a0), (a0))
WRAP_RET(int, gnutls_x509_aia_get, (gnutls_x509_aia_t a0, unsigned int a1, gnutls_datum_t * a2, unsigned int * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_aia_init, (gnutls_x509_aia_t * a0), (a0), -1)
WRAP_RET(int, gnutls_x509_aia_set, (gnutls_x509_aia_t a0, const char * a1, unsigned int a2, const gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_VOID(gnutls_x509_aki_deinit, (gnutls_x509_aki_t a0), (a0))
WRAP_RET(int, gnutls_x509_aki_get_cert_issuer, (gnutls_x509_aki_t a0, unsigned int a1, unsigned int * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_aki_get_id, (gnutls_x509_aki_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_aki_init, (gnutls_x509_aki_t * a0), (a0), -1)
WRAP_RET(int, gnutls_x509_aki_set_cert_issuer, (gnutls_x509_aki_t a0, unsigned int a1, const gnutls_datum_t * a2, const char * a3, const gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_aki_set_id, (gnutls_x509_aki_t a0, const gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_cidr_to_rfc5280, (const char * a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(unsigned int, gnutls_x509_crl_check_issuer, (gnutls_x509_crl_t a0, gnutls_x509_crt_t a1), (a0, a1), 0)
WRAP_VOID(gnutls_x509_crl_deinit, (gnutls_x509_crl_t a0), (a0))
WRAP_VOID(gnutls_x509_crl_dist_points_deinit, (gnutls_x509_crl_dist_points_t a0), (a0))
WRAP_RET(int, gnutls_x509_crl_dist_points_get, (gnutls_x509_crl_dist_points_t a0, unsigned int a1, unsigned int * a2, gnutls_datum_t * a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crl_dist_points_init, (gnutls_x509_crl_dist_points_t * a0), (a0), -1)
WRAP_RET(int, gnutls_x509_crl_dist_points_set, (gnutls_x509_crl_dist_points_t a0, gnutls_x509_subject_alt_name_t a1, const gnutls_datum_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crl_export, (gnutls_x509_crl_t a0, gnutls_x509_crt_fmt_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crl_export2, (gnutls_x509_crl_t a0, gnutls_x509_crt_fmt_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crl_get_authority_key_gn_serial, (gnutls_x509_crl_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4, void * a5, size_t * a6, unsigned int * a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(int, gnutls_x509_crl_get_authority_key_id, (gnutls_x509_crl_t a0, void * a1, size_t * a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crl_get_crt_count, (gnutls_x509_crl_t a0), (a0), -1)
WRAP_RET(int, gnutls_x509_crl_get_crt_serial, (gnutls_x509_crl_t a0, unsigned int a1, unsigned char * a2, size_t * a3, time_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crl_get_dn_oid, (gnutls_x509_crl_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crl_get_extension_data, (gnutls_x509_crl_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crl_get_extension_data2, (gnutls_x509_crl_t a0, unsigned int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crl_get_extension_info, (gnutls_x509_crl_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crl_get_extension_oid, (gnutls_x509_crl_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crl_get_issuer_dn, (gnutls_x509_crl_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crl_get_issuer_dn2, (gnutls_x509_crl_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crl_get_issuer_dn3, (gnutls_x509_crl_t a0, gnutls_datum_t * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crl_get_issuer_dn_by_oid, (gnutls_x509_crl_t a0, const char * a1, unsigned int a2, unsigned int a3, void * a4, size_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(time_t, gnutls_x509_crl_get_next_update, (gnutls_x509_crl_t a0), (a0), ((time_t)0))
WRAP_RET(int, gnutls_x509_crl_get_number, (gnutls_x509_crl_t a0, void * a1, size_t * a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crl_get_raw_issuer_dn, (gnutls_x509_crl_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crl_get_signature, (gnutls_x509_crl_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crl_get_signature_algorithm, (gnutls_x509_crl_t a0), (a0), -1)
WRAP_RET(int, gnutls_x509_crl_get_signature_oid, (gnutls_x509_crl_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(time_t, gnutls_x509_crl_get_this_update, (gnutls_x509_crl_t a0), (a0), ((time_t)0))
WRAP_RET(int, gnutls_x509_crl_get_version, (gnutls_x509_crl_t a0), (a0), -1)
WRAP_RET(int, gnutls_x509_crl_import, (gnutls_x509_crl_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crl_init, (gnutls_x509_crl_t * a0), (a0), -1)
WRAP_RET(int, gnutls_x509_crl_iter_crt_serial, (gnutls_x509_crl_t a0, gnutls_x509_crl_iter_t * a1, unsigned char * a2, size_t * a3, time_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_VOID(gnutls_x509_crl_iter_deinit, (gnutls_x509_crl_iter_t a0), (a0))
WRAP_RET(int, gnutls_x509_crl_list_import, (gnutls_x509_crl_t * a0, unsigned int * a1, const gnutls_datum_t * a2, gnutls_x509_crt_fmt_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crl_list_import2, (gnutls_x509_crl_t ** a0, unsigned int * a1, const gnutls_datum_t * a2, gnutls_x509_crt_fmt_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crl_print, (gnutls_x509_crl_t a0, gnutls_certificate_print_formats_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crl_privkey_sign, (gnutls_x509_crl_t a0, gnutls_x509_crt_t a1, gnutls_privkey_t a2, gnutls_digest_algorithm_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crl_set_authority_key_id, (gnutls_x509_crl_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crl_set_crt, (gnutls_x509_crl_t a0, gnutls_x509_crt_t a1, time_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crl_set_crt_serial, (gnutls_x509_crl_t a0, const void * a1, size_t a2, time_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crl_set_next_update, (gnutls_x509_crl_t a0, time_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crl_set_number, (gnutls_x509_crl_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crl_set_this_update, (gnutls_x509_crl_t a0, time_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crl_set_version, (gnutls_x509_crl_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crl_sign, (gnutls_x509_crl_t a0, gnutls_x509_crt_t a1, gnutls_x509_privkey_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crl_sign2, (gnutls_x509_crl_t a0, gnutls_x509_crt_t a1, gnutls_x509_privkey_t a2, gnutls_digest_algorithm_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crl_verify, (gnutls_x509_crl_t a0, const gnutls_x509_crt_t * a1, unsigned int a2, unsigned int a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_VOID(gnutls_x509_crq_deinit, (gnutls_x509_crq_t a0), (a0))
WRAP_RET(int, gnutls_x509_crq_export, (gnutls_x509_crq_t a0, gnutls_x509_crt_fmt_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_export2, (gnutls_x509_crq_t a0, gnutls_x509_crt_fmt_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_get_attribute_by_oid, (gnutls_x509_crq_t a0, const char * a1, unsigned int a2, void * a3, size_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crq_get_attribute_data, (gnutls_x509_crq_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_get_attribute_info, (gnutls_x509_crq_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_get_basic_constraints, (gnutls_x509_crq_t a0, unsigned int * a1, unsigned int * a2, int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_get_challenge_password, (gnutls_x509_crq_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_get_dn, (gnutls_x509_crq_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_get_dn2, (gnutls_x509_crq_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crq_get_dn3, (gnutls_x509_crq_t a0, gnutls_datum_t * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_get_dn_by_oid, (gnutls_x509_crq_t a0, const char * a1, unsigned int a2, unsigned int a3, void * a4, size_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_crq_get_dn_oid, (gnutls_x509_crq_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_get_extension_by_oid, (gnutls_x509_crq_t a0, const char * a1, unsigned int a2, void * a3, size_t * a4, unsigned int * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_crq_get_extension_by_oid2, (gnutls_x509_crq_t a0, const char * a1, unsigned int a2, gnutls_datum_t * a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crq_get_extension_data, (gnutls_x509_crq_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_get_extension_data2, (gnutls_x509_crq_t a0, unsigned int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_get_extension_info, (gnutls_x509_crq_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crq_get_key_id, (gnutls_x509_crq_t a0, unsigned int a1, unsigned char * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_get_key_purpose_oid, (gnutls_x509_crq_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crq_get_key_rsa_raw, (gnutls_x509_crq_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_get_key_usage, (gnutls_x509_crq_t a0, unsigned int * a1, unsigned int * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_get_pk_algorithm, (gnutls_x509_crq_t a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crq_get_pk_oid, (gnutls_x509_crq_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_get_private_key_usage_period, (gnutls_x509_crq_t a0, time_t * a1, time_t * a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_get_signature_algorithm, (gnutls_x509_crq_t a0), (a0), -1)
WRAP_RET(int, gnutls_x509_crq_get_signature_oid, (gnutls_x509_crq_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_get_spki, (gnutls_x509_crq_t a0, gnutls_x509_spki_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_get_subject_alt_name, (gnutls_x509_crq_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4, unsigned int * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_crq_get_subject_alt_othername_oid, (gnutls_x509_crq_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_get_tlsfeatures, (gnutls_x509_crq_t a0, gnutls_x509_tlsfeatures_t a1, unsigned int a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_get_version, (gnutls_x509_crq_t a0), (a0), -1)
WRAP_RET(int, gnutls_x509_crq_import, (gnutls_x509_crq_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_init, (gnutls_x509_crq_t * a0), (a0), -1)
WRAP_RET(int, gnutls_x509_crq_print, (gnutls_x509_crq_t a0, gnutls_certificate_print_formats_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_privkey_sign, (gnutls_x509_crq_t a0, gnutls_privkey_t a1, gnutls_digest_algorithm_t a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_set_attribute_by_oid, (gnutls_x509_crq_t a0, const char * a1, void * a2, size_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_set_basic_constraints, (gnutls_x509_crq_t a0, unsigned int a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_set_challenge_password, (gnutls_x509_crq_t a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crq_set_dn, (gnutls_x509_crq_t a0, const char * a1, const char ** a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_set_dn_by_oid, (gnutls_x509_crq_t a0, const char * a1, unsigned int a2, const void * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crq_set_extension_by_oid, (gnutls_x509_crq_t a0, const char * a1, const void * a2, size_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crq_set_key, (gnutls_x509_crq_t a0, gnutls_x509_privkey_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crq_set_key_purpose_oid, (gnutls_x509_crq_t a0, const void * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_set_key_rsa_raw, (gnutls_x509_crq_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_set_key_usage, (gnutls_x509_crq_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crq_set_private_key_usage_period, (gnutls_x509_crq_t a0, time_t a1, time_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_set_pubkey, (gnutls_x509_crq_t a0, gnutls_pubkey_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crq_set_spki, (gnutls_x509_crq_t a0, const gnutls_x509_spki_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crq_set_subject_alt_name, (gnutls_x509_crq_t a0, gnutls_x509_subject_alt_name_t a1, const void * a2, unsigned int a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crq_set_subject_alt_othername, (gnutls_x509_crq_t a0, const char * a1, const void * a2, unsigned int a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crq_set_tlsfeatures, (gnutls_x509_crq_t a0, gnutls_x509_tlsfeatures_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crq_set_version, (gnutls_x509_crq_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crq_sign, (gnutls_x509_crq_t a0, gnutls_x509_privkey_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crq_sign2, (gnutls_x509_crq_t a0, gnutls_x509_privkey_t a1, gnutls_digest_algorithm_t a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crq_verify, (gnutls_x509_crq_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(unsigned int, gnutls_x509_crt_check_email, (gnutls_x509_crt_t a0, const char * a1, unsigned int a2), (a0, a1, a2), 0)
WRAP_RET(unsigned int, gnutls_x509_crt_check_hostname, (gnutls_x509_crt_t a0, const char * a1), (a0, a1), 0)
WRAP_RET(unsigned int, gnutls_x509_crt_check_hostname2, (gnutls_x509_crt_t a0, const char * a1, unsigned int a2), (a0, a1, a2), 0)
WRAP_RET(unsigned int, gnutls_x509_crt_check_ip, (gnutls_x509_crt_t a0, const unsigned char * a1, unsigned int a2, unsigned int a3), (a0, a1, a2, a3), 0)
WRAP_RET(unsigned int, gnutls_x509_crt_check_issuer, (gnutls_x509_crt_t a0, gnutls_x509_crt_t a1), (a0, a1), 0)
WRAP_RET(unsigned int, gnutls_x509_crt_check_key_purpose, (gnutls_x509_crt_t a0, const char * a1, unsigned int a2), (a0, a1, a2), 0)
WRAP_RET(int, gnutls_x509_crt_check_revocation, (gnutls_x509_crt_t a0, const gnutls_x509_crl_t * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_cpy_crl_dist_points, (gnutls_x509_crt_t a0, gnutls_x509_crt_t a1), (a0, a1), -1)
WRAP_VOID(gnutls_x509_crt_deinit, (gnutls_x509_crt_t a0), (a0))
WRAP_RET(unsigned int, gnutls_x509_crt_equals, (gnutls_x509_crt_t a0, gnutls_x509_crt_t a1), (a0, a1), 0)
WRAP_RET(unsigned int, gnutls_x509_crt_equals2, (gnutls_x509_crt_t a0, const gnutls_datum_t * a1), (a0, a1), 0)
WRAP_RET(int, gnutls_x509_crt_export, (gnutls_x509_crt_t a0, gnutls_x509_crt_fmt_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_export2, (gnutls_x509_crt_t a0, gnutls_x509_crt_fmt_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(time_t, gnutls_x509_crt_get_activation_time, (gnutls_x509_crt_t a0), (a0), ((time_t)0))
WRAP_RET(int, gnutls_x509_crt_get_authority_info_access, (gnutls_x509_crt_t a0, unsigned int a1, int a2, gnutls_datum_t * a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_get_authority_key_gn_serial, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4, void * a5, size_t * a6, unsigned int * a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(int, gnutls_x509_crt_get_authority_key_id, (gnutls_x509_crt_t a0, void * a1, size_t * a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_basic_constraints, (gnutls_x509_crt_t a0, unsigned int * a1, unsigned int * a2, int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_ca_status, (gnutls_x509_crt_t a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_get_crl_dist_points, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4, unsigned int * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_crt_get_dn, (gnutls_x509_crt_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_dn2, (gnutls_x509_crt_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_get_dn3, (gnutls_x509_crt_t a0, gnutls_datum_t * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_dn_by_oid, (gnutls_x509_crt_t a0, const char * a1, unsigned int a2, unsigned int a3, void * a4, size_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_crt_get_dn_oid, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(time_t, gnutls_x509_crt_get_expiration_time, (gnutls_x509_crt_t a0), (a0), ((time_t)0))
WRAP_RET(int, gnutls_x509_crt_get_extension_by_oid, (gnutls_x509_crt_t a0, const char * a1, unsigned int a2, void * a3, size_t * a4, unsigned int * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_crt_get_extension_by_oid2, (gnutls_x509_crt_t a0, const char * a1, unsigned int a2, gnutls_datum_t * a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_get_extension_data, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_extension_data2, (gnutls_x509_crt_t a0, unsigned int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_extension_info, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_get_extension_oid, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_fingerprint, (gnutls_x509_crt_t a0, gnutls_digest_algorithm_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_inhibit_anypolicy, (gnutls_x509_crt_t a0, unsigned int * a1, unsigned int * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_issuer, (gnutls_x509_crt_t a0, gnutls_x509_dn_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_get_issuer_alt_name, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_get_issuer_alt_name2, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4, unsigned int * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_crt_get_issuer_alt_othername_oid, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_issuer_dn, (gnutls_x509_crt_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_issuer_dn2, (gnutls_x509_crt_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_get_issuer_dn3, (gnutls_x509_crt_t a0, gnutls_datum_t * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_issuer_dn_by_oid, (gnutls_x509_crt_t a0, const char * a1, unsigned int a2, unsigned int a3, void * a4, size_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_crt_get_issuer_dn_oid, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_issuer_unique_id, (gnutls_x509_crt_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_key_id, (gnutls_x509_crt_t a0, unsigned int a1, unsigned char * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_key_purpose_oid, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_get_key_usage, (gnutls_x509_crt_t a0, unsigned int * a1, unsigned int * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_name_constraints, (gnutls_x509_crt_t a0, gnutls_x509_name_constraints_t a1, unsigned int a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_pk_algorithm, (gnutls_x509_crt_t a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_get_pk_dsa_raw, (gnutls_x509_crt_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_get_pk_ecc_raw, (gnutls_x509_crt_t a0, gnutls_ecc_curve_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_pk_gost_raw, (gnutls_x509_crt_t a0, gnutls_ecc_curve_t * a1, gnutls_digest_algorithm_t * a2, gnutls_gost_paramset_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_crt_get_pk_oid, (gnutls_x509_crt_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_pk_rsa_raw, (gnutls_x509_crt_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_policy, (gnutls_x509_crt_t a0, unsigned int a1, struct gnutls_x509_policy_st * a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_preferred_hash_algorithm, (gnutls_x509_crt_t a0, gnutls_digest_algorithm_t * a1, unsigned int * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_private_key_usage_period, (gnutls_x509_crt_t a0, time_t * a1, time_t * a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_proxy, (gnutls_x509_crt_t a0, unsigned int * a1, int * a2, char ** a3, char ** a4, size_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_crt_get_raw_dn, (gnutls_x509_crt_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_get_raw_issuer_dn, (gnutls_x509_crt_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_get_serial, (gnutls_x509_crt_t a0, void * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_signature, (gnutls_x509_crt_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_signature_algorithm, (gnutls_x509_crt_t a0), (a0), -1)
WRAP_RET(int, gnutls_x509_crt_get_signature_oid, (gnutls_x509_crt_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_spki, (gnutls_x509_crt_t a0, gnutls_x509_spki_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_subject, (gnutls_x509_crt_t a0, gnutls_x509_dn_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_get_subject_alt_name, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_get_subject_alt_name2, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3, unsigned int * a4, unsigned int * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_crt_get_subject_alt_othername_oid, (gnutls_x509_crt_t a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_subject_key_id, (gnutls_x509_crt_t a0, void * a1, size_t * a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_subject_unique_id, (gnutls_x509_crt_t a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_get_tlsfeatures, (gnutls_x509_crt_t a0, gnutls_x509_tlsfeatures_t a1, unsigned int a2, unsigned int * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_get_version, (gnutls_x509_crt_t a0), (a0), -1)
WRAP_RET(int, gnutls_x509_crt_import, (gnutls_x509_crt_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_import_pkcs11, (gnutls_x509_crt_t a0, gnutls_pkcs11_obj_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_import_url, (gnutls_x509_crt_t a0, const char * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_init, (gnutls_x509_crt_t * a0), (a0), -1)
WRAP_RET(int, gnutls_x509_crt_list_import, (gnutls_x509_crt_t * a0, unsigned int * a1, const gnutls_datum_t * a2, gnutls_x509_crt_fmt_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_list_import2, (gnutls_x509_crt_t ** a0, unsigned int * a1, const gnutls_datum_t * a2, gnutls_x509_crt_fmt_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_list_import_pkcs11, (gnutls_x509_crt_t * a0, unsigned int a1, gnutls_pkcs11_obj_t *const a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_list_import_url, (gnutls_x509_crt_t ** a0, unsigned int * a1, const char * a2, gnutls_pin_callback_t a3, void * a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_crt_list_verify, (const gnutls_x509_crt_t * a0, unsigned int a1, const gnutls_x509_crt_t * a2, unsigned int a3, const gnutls_x509_crl_t * a4, unsigned int a5, unsigned int a6, unsigned int * a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(int, gnutls_x509_crt_print, (gnutls_x509_crt_t a0, gnutls_certificate_print_formats_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_privkey_sign, (gnutls_x509_crt_t a0, gnutls_x509_crt_t a1, gnutls_privkey_t a2, gnutls_digest_algorithm_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_set_activation_time, (gnutls_x509_crt_t a0, time_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_set_authority_info_access, (gnutls_x509_crt_t a0, int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_authority_key_id, (gnutls_x509_crt_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_basic_constraints, (gnutls_x509_crt_t a0, unsigned int a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_ca_status, (gnutls_x509_crt_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_set_crl_dist_points, (gnutls_x509_crt_t a0, gnutls_x509_subject_alt_name_t a1, const void * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_set_crl_dist_points2, (gnutls_x509_crt_t a0, gnutls_x509_subject_alt_name_t a1, const void * a2, unsigned int a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_set_crq, (gnutls_x509_crt_t a0, gnutls_x509_crq_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_set_crq_extension_by_oid, (gnutls_x509_crt_t a0, gnutls_x509_crq_t a1, const char * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_crt_set_crq_extensions, (gnutls_x509_crt_t a0, gnutls_x509_crq_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_set_dn, (gnutls_x509_crt_t a0, const char * a1, const char ** a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_dn_by_oid, (gnutls_x509_crt_t a0, const char * a1, unsigned int a2, const void * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_set_expiration_time, (gnutls_x509_crt_t a0, time_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_set_extension_by_oid, (gnutls_x509_crt_t a0, const char * a1, const void * a2, size_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_VOID(gnutls_x509_crt_set_flags, (gnutls_x509_crt_t a0, unsigned int a1), (a0, a1))
WRAP_RET(int, gnutls_x509_crt_set_inhibit_anypolicy, (gnutls_x509_crt_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_set_issuer_alt_name, (gnutls_x509_crt_t a0, gnutls_x509_subject_alt_name_t a1, const void * a2, unsigned int a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_set_issuer_alt_othername, (gnutls_x509_crt_t a0, const char * a1, const void * a2, unsigned int a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_set_issuer_dn, (gnutls_x509_crt_t a0, const char * a1, const char ** a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_issuer_dn_by_oid, (gnutls_x509_crt_t a0, const char * a1, unsigned int a2, const void * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_set_issuer_unique_id, (gnutls_x509_crt_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_key, (gnutls_x509_crt_t a0, gnutls_x509_privkey_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_set_key_purpose_oid, (gnutls_x509_crt_t a0, const void * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_key_usage, (gnutls_x509_crt_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_set_name_constraints, (gnutls_x509_crt_t a0, gnutls_x509_name_constraints_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_x509_crt_set_pin_function, (gnutls_x509_crt_t a0, gnutls_pin_callback_t a1, void * a2), (a0, a1, a2))
WRAP_RET(int, gnutls_x509_crt_set_policy, (gnutls_x509_crt_t a0, const struct gnutls_x509_policy_st * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_private_key_usage_period, (gnutls_x509_crt_t a0, time_t a1, time_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_proxy, (gnutls_x509_crt_t a0, int a1, const char * a2, const char * a3, size_t a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_set_proxy_dn, (gnutls_x509_crt_t a0, gnutls_x509_crt_t a1, unsigned int a2, const void * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_set_pubkey, (gnutls_x509_crt_t a0, gnutls_pubkey_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_set_serial, (gnutls_x509_crt_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_spki, (gnutls_x509_crt_t a0, const gnutls_x509_spki_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_subject_alt_name, (gnutls_x509_crt_t a0, gnutls_x509_subject_alt_name_t a1, const void * a2, unsigned int a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_set_subject_alt_othername, (gnutls_x509_crt_t a0, const char * a1, const void * a2, unsigned int a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_set_subject_alternative_name, (gnutls_x509_crt_t a0, gnutls_x509_subject_alt_name_t a1, const char * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_subject_key_id, (gnutls_x509_crt_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_subject_unique_id, (gnutls_x509_crt_t a0, const void * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_set_tlsfeatures, (gnutls_x509_crt_t a0, gnutls_x509_tlsfeatures_t a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_set_version, (gnutls_x509_crt_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_crt_sign, (gnutls_x509_crt_t a0, gnutls_x509_crt_t a1, gnutls_x509_privkey_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_crt_sign2, (gnutls_x509_crt_t a0, gnutls_x509_crt_t a1, gnutls_x509_privkey_t a2, gnutls_digest_algorithm_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_verify, (gnutls_x509_crt_t a0, const gnutls_x509_crt_t * a1, unsigned int a2, unsigned int a3, unsigned int * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_crt_verify_data2, (gnutls_x509_crt_t a0, gnutls_sign_algorithm_t a1, unsigned int a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_ct_sct_get, (const gnutls_x509_ct_scts_t a0, unsigned int a1, time_t * a2, gnutls_datum_t * a3, gnutls_sign_algorithm_t * a4, gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_ct_sct_get_version, (const gnutls_x509_ct_scts_t a0, unsigned int a1, unsigned int * a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_x509_dn_deinit, (gnutls_x509_dn_t a0), (a0))
WRAP_RET(int, gnutls_x509_dn_export, (gnutls_x509_dn_t a0, gnutls_x509_crt_fmt_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_dn_export2, (gnutls_x509_dn_t a0, gnutls_x509_crt_fmt_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_dn_get_rdn_ava, (gnutls_x509_dn_t a0, int a1, int a2, gnutls_x509_ava_st * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_dn_get_str, (gnutls_x509_dn_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_dn_get_str2, (gnutls_x509_dn_t a0, gnutls_datum_t * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_dn_import, (gnutls_x509_dn_t a0, const gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_dn_init, (gnutls_x509_dn_t * a0), (a0), -1)
WRAP_RET(int, gnutls_x509_dn_oid_known, (const char * a0), (a0), -1)
WRAP_RET(const char *, gnutls_x509_dn_oid_name, (const char * a0, unsigned int a1), (a0, a1), NULL)
WRAP_RET(int, gnutls_x509_dn_set_str, (gnutls_x509_dn_t a0, const char * a1, const char ** a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_ct_export_scts, (const gnutls_x509_ct_scts_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_ct_import_scts, (const gnutls_datum_t * a0, gnutls_x509_ct_scts_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_x509_ext_ct_scts_deinit, (gnutls_x509_ct_scts_t a0), (a0))
WRAP_RET(int, gnutls_x509_ext_ct_scts_init, (gnutls_x509_ct_scts_t * a0), (a0), -1)
WRAP_VOID(gnutls_x509_ext_deinit, (gnutls_x509_ext_st * a0), (a0))
WRAP_RET(int, gnutls_x509_ext_export_aia, (gnutls_x509_aia_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_export_authority_key_id, (gnutls_x509_aki_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_export_basic_constraints, (unsigned int a0, int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_export_crl_dist_points, (gnutls_x509_crl_dist_points_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_export_inhibit_anypolicy, (unsigned int a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_export_key_purposes, (gnutls_x509_key_purposes_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_export_key_usage, (unsigned int a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_export_name_constraints, (gnutls_x509_name_constraints_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_export_policies, (gnutls_x509_policies_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_export_private_key_usage_period, (time_t a0, time_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_export_proxy, (int a0, const char * a1, const char * a2, size_t a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_ext_export_subject_alt_names, (gnutls_subject_alt_names_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_export_subject_key_id, (const gnutls_datum_t * a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_export_tlsfeatures, (gnutls_x509_tlsfeatures_t a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_import_aia, (const gnutls_datum_t * a0, gnutls_x509_aia_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_import_authority_key_id, (const gnutls_datum_t * a0, gnutls_x509_aki_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_import_basic_constraints, (const gnutls_datum_t * a0, unsigned int * a1, int * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_import_crl_dist_points, (const gnutls_datum_t * a0, gnutls_x509_crl_dist_points_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_import_inhibit_anypolicy, (const gnutls_datum_t * a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_import_key_purposes, (const gnutls_datum_t * a0, gnutls_x509_key_purposes_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_import_key_usage, (const gnutls_datum_t * a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_import_name_constraints, (const gnutls_datum_t * a0, gnutls_x509_name_constraints_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_import_policies, (const gnutls_datum_t * a0, gnutls_x509_policies_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_import_private_key_usage_period, (const gnutls_datum_t * a0, time_t * a1, time_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_import_proxy, (const gnutls_datum_t * a0, int * a1, char ** a2, char ** a3, size_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_ext_import_subject_alt_names, (const gnutls_datum_t * a0, gnutls_subject_alt_names_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_import_subject_key_id, (const gnutls_datum_t * a0, gnutls_datum_t * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_ext_import_tlsfeatures, (const gnutls_datum_t * a0, gnutls_x509_tlsfeatures_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_ext_print, (gnutls_x509_ext_st * a0, unsigned int a1, gnutls_certificate_print_formats_t a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_VOID(gnutls_x509_key_purpose_deinit, (gnutls_x509_key_purposes_t a0), (a0))
WRAP_RET(int, gnutls_x509_key_purpose_get, (gnutls_x509_key_purposes_t a0, unsigned int a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_key_purpose_init, (gnutls_x509_key_purposes_t * a0), (a0), -1)
WRAP_RET(int, gnutls_x509_key_purpose_set, (gnutls_x509_key_purposes_t a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_name_constraints_add_excluded, (gnutls_x509_name_constraints_t a0, gnutls_x509_subject_alt_name_t a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_name_constraints_add_permitted, (gnutls_x509_name_constraints_t a0, gnutls_x509_subject_alt_name_t a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(unsigned int, gnutls_x509_name_constraints_check, (gnutls_x509_name_constraints_t a0, gnutls_x509_subject_alt_name_t a1, const gnutls_datum_t * a2), (a0, a1, a2), 0)
WRAP_RET(unsigned int, gnutls_x509_name_constraints_check_crt, (gnutls_x509_name_constraints_t a0, gnutls_x509_subject_alt_name_t a1, gnutls_x509_crt_t a2), (a0, a1, a2), 0)
WRAP_VOID(gnutls_x509_name_constraints_deinit, (gnutls_x509_name_constraints_t a0), (a0))
WRAP_RET(int, gnutls_x509_name_constraints_get_excluded, (gnutls_x509_name_constraints_t a0, unsigned int a1, unsigned int * a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_name_constraints_get_permitted, (gnutls_x509_name_constraints_t a0, unsigned int a1, unsigned int * a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_name_constraints_init, (gnutls_x509_name_constraints_t * a0), (a0), -1)
WRAP_RET(int, gnutls_x509_othername_to_virtual, (const char * a0, const gnutls_datum_t * a1, unsigned int * a2, gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_VOID(gnutls_x509_policies_deinit, (gnutls_x509_policies_t a0), (a0))
WRAP_RET(int, gnutls_x509_policies_get, (gnutls_x509_policies_t a0, unsigned int a1, struct gnutls_x509_policy_st * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_policies_init, (gnutls_x509_policies_t * a0), (a0), -1)
WRAP_RET(int, gnutls_x509_policies_set, (gnutls_x509_policies_t a0, const struct gnutls_x509_policy_st * a1), (a0, a1), -1)
WRAP_VOID(gnutls_x509_policy_release, (struct gnutls_x509_policy_st * a0), (a0))
WRAP_RET(int, gnutls_x509_privkey_cpy, (gnutls_x509_privkey_t a0, gnutls_x509_privkey_t a1), (a0, a1), -1)
WRAP_VOID(gnutls_x509_privkey_deinit, (gnutls_x509_privkey_t a0), (a0))
WRAP_RET(int, gnutls_x509_privkey_export, (gnutls_x509_privkey_t a0, gnutls_x509_crt_fmt_t a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_privkey_export2, (gnutls_x509_privkey_t a0, gnutls_x509_crt_fmt_t a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_privkey_export2_pkcs8, (gnutls_x509_privkey_t a0, gnutls_x509_crt_fmt_t a1, const char * a2, unsigned int a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_privkey_export_dsa_raw, (gnutls_x509_privkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_privkey_export_ecc_raw, (gnutls_x509_privkey_t a0, gnutls_ecc_curve_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_privkey_export_gost_raw, (gnutls_x509_privkey_t a0, gnutls_ecc_curve_t * a1, gnutls_digest_algorithm_t * a2, gnutls_gost_paramset_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, gnutls_datum_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_x509_privkey_export_pkcs8, (gnutls_x509_privkey_t a0, gnutls_x509_crt_fmt_t a1, const char * a2, unsigned int a3, void * a4, size_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_privkey_export_rsa_raw, (gnutls_x509_privkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, gnutls_datum_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_x509_privkey_export_rsa_raw2, (gnutls_x509_privkey_t a0, gnutls_datum_t * a1, gnutls_datum_t * a2, gnutls_datum_t * a3, gnutls_datum_t * a4, gnutls_datum_t * a5, gnutls_datum_t * a6, gnutls_datum_t * a7, gnutls_datum_t * a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_RET(int, gnutls_x509_privkey_fix, (gnutls_x509_privkey_t a0), (a0), -1)
WRAP_RET(int, gnutls_x509_privkey_generate, (gnutls_x509_privkey_t a0, gnutls_pk_algorithm_t a1, unsigned int a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_privkey_generate2, (gnutls_x509_privkey_t a0, gnutls_pk_algorithm_t a1, unsigned int a2, unsigned int a3, const gnutls_keygen_data_st * a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_privkey_get_key_id, (gnutls_x509_privkey_t a0, unsigned int a1, unsigned char * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_privkey_get_pk_algorithm, (gnutls_x509_privkey_t a0), (a0), -1)
WRAP_RET(int, gnutls_x509_privkey_get_pk_algorithm2, (gnutls_x509_privkey_t a0, unsigned int * a1), (a0, a1), -1)
WRAP_RET(int, gnutls_x509_privkey_get_seed, (gnutls_x509_privkey_t a0, gnutls_digest_algorithm_t * a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_privkey_get_spki, (gnutls_x509_privkey_t a0, gnutls_x509_spki_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_privkey_import, (gnutls_x509_privkey_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_privkey_import2, (gnutls_x509_privkey_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2, const char * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_privkey_import_dh_raw, (gnutls_x509_privkey_t a0, const gnutls_dh_params_t a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_privkey_import_dsa_raw, (gnutls_x509_privkey_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4, const gnutls_datum_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_privkey_import_ecc_raw, (gnutls_x509_privkey_t a0, gnutls_ecc_curve_t a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_privkey_import_gost_raw, (gnutls_x509_privkey_t a0, gnutls_ecc_curve_t a1, gnutls_digest_algorithm_t a2, gnutls_gost_paramset_t a3, const gnutls_datum_t * a4, const gnutls_datum_t * a5, const gnutls_datum_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_x509_privkey_import_openssl, (gnutls_x509_privkey_t a0, const gnutls_datum_t * a1, const char * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_privkey_import_pkcs8, (gnutls_x509_privkey_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2, const char * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_privkey_import_rsa_raw, (gnutls_x509_privkey_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4, const gnutls_datum_t * a5, const gnutls_datum_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, gnutls_x509_privkey_import_rsa_raw2, (gnutls_x509_privkey_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, const gnutls_datum_t * a3, const gnutls_datum_t * a4, const gnutls_datum_t * a5, const gnutls_datum_t * a6, const gnutls_datum_t * a7, const gnutls_datum_t * a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
WRAP_RET(int, gnutls_x509_privkey_init, (gnutls_x509_privkey_t * a0), (a0), -1)
WRAP_RET(gnutls_sec_param_t, gnutls_x509_privkey_sec_param, (gnutls_x509_privkey_t a0), (a0), ((gnutls_sec_param_t)0))
WRAP_VOID(gnutls_x509_privkey_set_flags, (gnutls_x509_privkey_t a0, unsigned int a1), (a0, a1))
WRAP_VOID(gnutls_x509_privkey_set_pin_function, (gnutls_x509_privkey_t a0, gnutls_pin_callback_t a1, void * a2), (a0, a1, a2))
WRAP_RET(int, gnutls_x509_privkey_set_spki, (gnutls_x509_privkey_t a0, const gnutls_x509_spki_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_privkey_sign_data, (gnutls_x509_privkey_t a0, gnutls_digest_algorithm_t a1, unsigned int a2, const gnutls_datum_t * a3, void * a4, size_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_privkey_sign_hash, (gnutls_x509_privkey_t a0, const gnutls_datum_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_privkey_verify_params, (gnutls_x509_privkey_t a0), (a0), -1)
WRAP_RET(int, gnutls_x509_privkey_verify_seed, (gnutls_x509_privkey_t a0, gnutls_digest_algorithm_t a1, const void * a2, size_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_rdn_get, (const gnutls_datum_t * a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_rdn_get2, (const gnutls_datum_t * a0, gnutls_datum_t * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_rdn_get_by_oid, (const gnutls_datum_t * a0, const char * a1, unsigned int a2, unsigned int a3, void * a4, size_t * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_rdn_get_oid, (const gnutls_datum_t * a0, unsigned int a1, void * a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_VOID(gnutls_x509_spki_deinit, (gnutls_x509_spki_t a0), (a0))
WRAP_RET(int, gnutls_x509_spki_get_rsa_oaep_params, (gnutls_x509_spki_t a0, gnutls_digest_algorithm_t * a1, gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_spki_get_rsa_pss_params, (gnutls_x509_spki_t a0, gnutls_digest_algorithm_t * a1, unsigned int * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_spki_init, (gnutls_x509_spki_t * a0), (a0), -1)
WRAP_RET(int, gnutls_x509_spki_set_rsa_oaep_params, (gnutls_x509_spki_t a0, gnutls_digest_algorithm_t a1, const gnutls_datum_t * a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_x509_spki_set_rsa_pss_params, (gnutls_x509_spki_t a0, gnutls_digest_algorithm_t a1, unsigned int a2), (a0, a1, a2))
WRAP_RET(int, gnutls_x509_tlsfeatures_add, (gnutls_x509_tlsfeatures_t a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(unsigned int, gnutls_x509_tlsfeatures_check_crt, (gnutls_x509_tlsfeatures_t a0, gnutls_x509_crt_t a1), (a0, a1), 0)
WRAP_VOID(gnutls_x509_tlsfeatures_deinit, (gnutls_x509_tlsfeatures_t a0), (a0))
WRAP_RET(int, gnutls_x509_tlsfeatures_get, (gnutls_x509_tlsfeatures_t a0, unsigned int a1, unsigned int * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_tlsfeatures_init, (gnutls_x509_tlsfeatures_t * a0), (a0), -1)
WRAP_RET(int, gnutls_x509_trust_list_add_cas, (gnutls_x509_trust_list_t a0, const gnutls_x509_crt_t * a1, unsigned int a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_trust_list_add_crls, (gnutls_x509_trust_list_t a0, const gnutls_x509_crl_t * a1, unsigned int a2, unsigned int a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_trust_list_add_named_crt, (gnutls_x509_trust_list_t a0, gnutls_x509_crt_t a1, const void * a2, size_t a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, gnutls_x509_trust_list_add_system_trust, (gnutls_x509_trust_list_t a0, unsigned int a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_trust_list_add_trust_dir, (gnutls_x509_trust_list_t a0, const char * a1, const char * a2, gnutls_x509_crt_fmt_t a3, unsigned int a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_trust_list_add_trust_file, (gnutls_x509_trust_list_t a0, const char * a1, const char * a2, gnutls_x509_crt_fmt_t a3, unsigned int a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_trust_list_add_trust_mem, (gnutls_x509_trust_list_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, gnutls_x509_crt_fmt_t a3, unsigned int a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_VOID(gnutls_x509_trust_list_deinit, (gnutls_x509_trust_list_t a0, unsigned int a1), (a0, a1))
WRAP_RET(int, gnutls_x509_trust_list_get_issuer, (gnutls_x509_trust_list_t a0, gnutls_x509_crt_t a1, gnutls_x509_crt_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_trust_list_get_issuer_by_dn, (gnutls_x509_trust_list_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_t * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, gnutls_x509_trust_list_get_issuer_by_subject_key_id, (gnutls_x509_trust_list_t a0, const gnutls_datum_t * a1, const gnutls_datum_t * a2, gnutls_x509_crt_t * a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(void *, gnutls_x509_trust_list_get_ptr, (gnutls_x509_trust_list_t a0), (a0), NULL)
WRAP_RET(int, gnutls_x509_trust_list_init, (gnutls_x509_trust_list_t * a0, unsigned int a1), (a0, a1), -1)
WRAP_VOID(gnutls_x509_trust_list_iter_deinit, (gnutls_x509_trust_list_iter_t a0), (a0))
WRAP_RET(int, gnutls_x509_trust_list_iter_get_ca, (gnutls_x509_trust_list_t a0, gnutls_x509_trust_list_iter_t * a1, gnutls_x509_crt_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_trust_list_remove_cas, (gnutls_x509_trust_list_t a0, const gnutls_x509_crt_t * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_trust_list_remove_trust_file, (gnutls_x509_trust_list_t a0, const char * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_RET(int, gnutls_x509_trust_list_remove_trust_mem, (gnutls_x509_trust_list_t a0, const gnutls_datum_t * a1, gnutls_x509_crt_fmt_t a2), (a0, a1, a2), -1)
WRAP_VOID(gnutls_x509_trust_list_set_getissuer_function, (gnutls_x509_trust_list_t a0, gnutls_x509_trust_list_getissuer_function (*a1)), (a0, a1))
WRAP_VOID(gnutls_x509_trust_list_set_ptr, (gnutls_x509_trust_list_t a0, void * a1), (a0, a1))
WRAP_RET(int, gnutls_x509_trust_list_verify_crt, (gnutls_x509_trust_list_t a0, gnutls_x509_crt_t * a1, unsigned int a2, unsigned int a3, unsigned int * a4, gnutls_verify_output_function (*a5)), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, gnutls_x509_trust_list_verify_crt2, (gnutls_x509_trust_list_t a0, gnutls_x509_crt_t * a1, unsigned int a2, gnutls_typed_vdata_st * a3, unsigned int a4, unsigned int a5, unsigned int * a6, gnutls_verify_output_function (*a7)), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
WRAP_RET(int, gnutls_x509_trust_list_verify_named_crt, (gnutls_x509_trust_list_t a0, gnutls_x509_crt_t a1, const void * a2, size_t a3, unsigned int a4, unsigned int * a5, gnutls_verify_output_function (*a6)), (a0, a1, a2, a3, a4, a5, a6), -1)

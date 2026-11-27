/* ============================================================
 * mbedtls_dyn.c — MBEDTLS 动态加载封装层（公开头文件 API 全量封装）
 *
 * 统计：导出函数 1195 / 封装 431 / 跳过 764 / 数据符号 22 / 内联类型 173
 * 设计要点（与 rtdb/libxml2_dyn.c 一致）：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不链接目标库（只 -ldl）。
 *   2. 每个封装函数首次调用时才 dlsym 并缓存；未调用的 API 不解析。
 *   3. 编译期内联官方头类型（由 gcc -aux-info 核对签名），无目标库依赖。
 *   4. 加载顺序：显式路径 > MBEDTLS_DYN_LIB > LD_LIBRARY_PATH > soname > Nix > 平台默认。
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
typedef time_t mbedtls_time_t;
typedef int64_t mbedtls_ms_time_t;
typedef int32_t psa_status_t;
typedef uint16_t psa_key_type_t;
typedef uint8_t psa_ecc_family_t;
typedef uint32_t psa_algorithm_t;
typedef uint32_t psa_key_lifetime_t;
typedef uint32_t psa_key_id_t;
typedef psa_key_id_t mbedtls_svc_key_id_t;
typedef uint32_t psa_key_usage_t;
typedef struct psa_key_attributes_s psa_key_attributes_t;
typedef uint16_t psa_key_derivation_step_t;
typedef struct psa_custom_key_parameters_s psa_custom_key_parameters_t;
typedef enum {
    MBEDTLS_MD_NONE=0,
    MBEDTLS_MD_MD5=0x03,
    MBEDTLS_MD_RIPEMD160=0x04,
    MBEDTLS_MD_SHA1=0x05,
    MBEDTLS_MD_SHA224=0x08,
    MBEDTLS_MD_SHA256=0x09,
    MBEDTLS_MD_SHA384=0x0a,
    MBEDTLS_MD_SHA512=0x0b,
    MBEDTLS_MD_SHA3_224=0x10,
    MBEDTLS_MD_SHA3_256=0x11,
    MBEDTLS_MD_SHA3_384=0x12,
    MBEDTLS_MD_SHA3_512=0x13,
} mbedtls_md_type_t;
typedef struct mbedtls_md_info_t mbedtls_md_info_t;
typedef struct mbedtls_md_context_t {
    const mbedtls_md_info_t *private_md_info;
    void *private_md_ctx;
    void *private_hmac_ctx;
} mbedtls_md_context_t;
typedef struct mbedtls_md5_context {
    uint32_t private_total[2];
    uint32_t private_state[4];
    unsigned char private_buffer[64];
}
mbedtls_md5_context;
typedef struct mbedtls_ripemd160_context {
    uint32_t private_total[2];
    uint32_t private_state[5];
    unsigned char private_buffer[64];
}
mbedtls_ripemd160_context;
typedef struct mbedtls_sha1_context {
    uint32_t private_total[2];
    uint32_t private_state[5];
    unsigned char private_buffer[64];
}
mbedtls_sha1_context;
typedef struct mbedtls_sha256_context {
    unsigned char private_buffer[64];
    uint32_t private_total[2];
    uint32_t private_state[8];
    int private_is224;
}
mbedtls_sha256_context;
typedef struct mbedtls_sha512_context {
    uint64_t private_total[2];
    uint64_t private_state[8];
    unsigned char private_buffer[128];
    int private_is384;
}
mbedtls_sha512_context;
typedef struct {
    uint64_t private_state[25];
    uint32_t private_index;
    uint16_t private_olen;
    uint16_t private_max_block_size;
    uint8_t private_finished;
}
mbedtls_sha3_context;
typedef struct {
    psa_algorithm_t private_alg;
    union {
        unsigned dummy;
        mbedtls_md5_context md5;
        mbedtls_ripemd160_context ripemd160;
        mbedtls_sha1_context sha1;
        mbedtls_sha256_context sha256;
        mbedtls_sha512_context sha512;
        mbedtls_sha3_context sha3;
    } private_ctx;
} mbedtls_psa_hash_operation_t;
typedef struct {
    psa_algorithm_t private_alg;
    uint8_t have_output;
    union {
        unsigned dummy;
        mbedtls_sha3_context shake;
    } private_ctx;
} mbedtls_psa_xof_operation_t;
typedef enum {
    MBEDTLS_OPERATION_NONE = -1,
    MBEDTLS_DECRYPT = 0,
    MBEDTLS_ENCRYPT,
} mbedtls_operation_t;
typedef struct mbedtls_cmac_context_t mbedtls_cmac_context_t;
typedef struct mbedtls_cipher_info_t {
    const char *private_name;
    unsigned int private_block_size : 5;
    unsigned int private_iv_size : 3;
    unsigned int private_key_bitlen : 4;
    unsigned int private_mode : 4;
    unsigned int private_type : 8;
    unsigned int private_flags : 2;
    unsigned int private_base_idx : 5;
} mbedtls_cipher_info_t;
typedef struct mbedtls_cipher_context_t {
    const mbedtls_cipher_info_t *private_cipher_info;
    int private_key_bitlen;
    mbedtls_operation_t private_operation;
    void(*private_add_padding)(unsigned char *output, size_t olen,
                                        size_t data_len);
    int(*private_get_padding)(unsigned char *input, size_t ilen,
                                       size_t *data_len,
                                       size_t *invalid_padding);
    unsigned char private_unprocessed_data[16];
    size_t private_unprocessed_len;
    unsigned char private_iv[16];
    size_t private_iv_size;
    void *private_cipher_ctx;
    mbedtls_cmac_context_t *private_cmac_ctx;
} mbedtls_cipher_context_t;
typedef struct {
    psa_algorithm_t private_alg;
    uint8_t private_iv_length;
    uint8_t private_block_length;
    union {
        unsigned int private_dummy;
        mbedtls_cipher_context_t private_cipher;
    } private_ctx;
} mbedtls_psa_cipher_operation_t;
typedef union {
    unsigned dummy;
    mbedtls_psa_hash_operation_t mbedtls_ctx;
} psa_driver_hash_context_t;
typedef union {
    unsigned dummy;
    mbedtls_psa_xof_operation_t mbedtls_ctx;
} psa_driver_xof_context_t;
typedef union {
    unsigned dummy;
    mbedtls_psa_cipher_operation_t mbedtls_ctx;
} psa_driver_cipher_context_t;
struct psa_hash_operation_s {
    unsigned int private_id;
    psa_driver_hash_context_t private_ctx;
};
struct psa_xof_operation_s {
    unsigned int private_id;
    unsigned requires_context : 1;
    unsigned allows_context : 1;
    unsigned active : 1;
    unsigned has_context : 1;
    unsigned has_input : 1;
    unsigned has_output : 1;
    psa_driver_xof_context_t private_ctx;
};
struct psa_cipher_operation_s {
    unsigned int private_id;
    unsigned int private_iv_required : 1;
    unsigned int private_iv_set : 1;
    uint8_t private_default_iv_length;
    psa_driver_cipher_context_t private_ctx;
};
typedef struct mbedtls_gcm_context {
    mbedtls_cipher_context_t private_cipher_ctx;
    uint64_t private_H[16][2];
    uint64_t private_len;
    uint64_t private_add_len;
    unsigned char private_base_ectr[16];
    unsigned char private_y[16];
    unsigned char private_buf[16];
    unsigned char private_mode;
    unsigned char private_acceleration;
}
mbedtls_gcm_context;
typedef struct mbedtls_ccm_context {
    unsigned char private_y[16];
    unsigned char private_ctr[16];
    size_t private_plaintext_len;
    size_t private_add_len;
    size_t private_tag_len;
    size_t private_processed;
    unsigned int private_q;
    unsigned int private_mode;
    mbedtls_cipher_context_t private_cipher_ctx;
    int private_state;
}
mbedtls_ccm_context;
typedef struct mbedtls_poly1305_context {
    uint32_t private_r[4];
    uint32_t private_s[4];
    uint32_t private_acc[5];
    uint8_t private_queue[16];
    size_t private_queue_len;
}
mbedtls_poly1305_context;
typedef enum {
    MBEDTLS_CHACHAPOLY_ENCRYPT,
    MBEDTLS_CHACHAPOLY_DECRYPT
}
mbedtls_chachapoly_mode_t;
typedef struct mbedtls_chacha20_context {
    uint32_t private_state[16];
    uint8_t private_keystream8[64];
    size_t private_keystream_bytes_used;
}
mbedtls_chacha20_context;
typedef struct mbedtls_chachapoly_context {
    mbedtls_chacha20_context private_chacha20_ctx;
    mbedtls_poly1305_context private_poly1305_ctx;
    uint64_t private_aad_len;
    uint64_t private_ciphertext_len;
    int private_state;
    mbedtls_chachapoly_mode_t private_mode;
}
mbedtls_chachapoly_context;
typedef struct {
    psa_algorithm_t private_alg;
    struct psa_hash_operation_s hash_ctx;
    uint8_t private_opad[144u];
} mbedtls_psa_hmac_operation_t;
typedef struct {
    psa_algorithm_t private_alg;
    union {
        unsigned private_dummy;
        mbedtls_psa_hmac_operation_t private_hmac;
        mbedtls_cipher_context_t private_cmac;
    } private_ctx;
} mbedtls_psa_mac_operation_t;
typedef struct {
    psa_algorithm_t private_alg;
    psa_key_type_t private_key_type;
    unsigned int private_is_encrypt : 1;
    uint8_t private_tag_length;
    union {
        unsigned dummy;
        mbedtls_ccm_context private_ccm;
        mbedtls_gcm_context private_gcm;
        mbedtls_chachapoly_context private_chachapoly;
    } ctx;
} mbedtls_psa_aead_operation_t;
typedef uint64_t mbedtls_mpi_uint;
typedef struct mbedtls_mpi {
    mbedtls_mpi_uint *private_p;
    signed short private_s;
    unsigned short private_n;
}
mbedtls_mpi;
typedef enum {
    MBEDTLS_ECP_DP_NONE = 0,
    MBEDTLS_ECP_DP_SECP256R1,
    MBEDTLS_ECP_DP_SECP384R1,
    MBEDTLS_ECP_DP_SECP521R1,
    MBEDTLS_ECP_DP_BP256R1,
    MBEDTLS_ECP_DP_BP384R1,
    MBEDTLS_ECP_DP_BP512R1,
    MBEDTLS_ECP_DP_CURVE25519,
    MBEDTLS_ECP_DP_SECP256K1,
    MBEDTLS_ECP_DP_CURVE448,
} mbedtls_ecp_group_id;
typedef struct mbedtls_ecp_point {
    mbedtls_mpi private_X;
    mbedtls_mpi private_Y;
    mbedtls_mpi private_Z;
}
mbedtls_ecp_point;
typedef struct mbedtls_ecp_group {
    mbedtls_ecp_group_id id;
    mbedtls_mpi P;
    mbedtls_mpi A;
    mbedtls_mpi B;
    mbedtls_ecp_point G;
    mbedtls_mpi N;
    size_t pbits;
    size_t nbits;
    unsigned int private_h;
    int(*private_modp)(mbedtls_mpi *);
    int(*private_t_pre)(mbedtls_ecp_point *, void *);
    int(*private_t_post)(mbedtls_ecp_point *, void *);
    void *private_t_data;
    mbedtls_ecp_point *private_T;
    size_t private_T_size;
}
mbedtls_ecp_group;
typedef struct mbedtls_ecp_keypair {
    mbedtls_ecp_group private_grp;
    mbedtls_mpi private_d;
    mbedtls_ecp_point private_Q;
}
mbedtls_ecp_keypair;
typedef struct {
    unsigned private_dummy;
} mbedtls_psa_sign_hash_interruptible_operation_t;
typedef struct {
    unsigned private_dummy;
} mbedtls_psa_verify_hash_interruptible_operation_t;
typedef enum {
    MBEDTLS_ECJPAKE_CLIENT = 0,
    MBEDTLS_ECJPAKE_SERVER,
    MBEDTLS_ECJPAKE_NONE,
} mbedtls_ecjpake_role;
typedef struct mbedtls_ecjpake_context {
    mbedtls_md_type_t private_md_type;
    mbedtls_ecp_group private_grp;
    mbedtls_ecjpake_role private_role;
    int private_point_format;
    mbedtls_ecp_point private_Xm1;
    mbedtls_ecp_point private_Xm2;
    mbedtls_ecp_point private_Xp1;
    mbedtls_ecp_point private_Xp2;
    mbedtls_ecp_point private_Xp;
    mbedtls_mpi private_xm1;
    mbedtls_mpi private_xm2;
    mbedtls_mpi private_s;
} mbedtls_ecjpake_context;
typedef struct {
    psa_algorithm_t private_alg;
    uint8_t *private_password;
    size_t private_password_len;
    mbedtls_ecjpake_role private_role;
    uint8_t private_buffer[((3 + 1 + 65 + 1 + 65 + 1 + 32) * 2)];
    size_t private_buffer_length;
    size_t private_buffer_offset;
    union {
        unsigned int private_dummy;
        mbedtls_ecjpake_context private_jpake;
    } private_ctx;
} mbedtls_psa_pake_operation_t;
typedef struct {
    mbedtls_ecp_keypair private_ecp;
    uint32_t num_ops;
} mbedtls_psa_generate_key_iop_t;
typedef struct {
    unsigned private_dummy;
} mbedtls_psa_key_agreement_interruptible_operation_t;
typedef struct {
    unsigned private_dummy;
} mbedtls_psa_export_public_key_iop_t;
typedef union {
    unsigned dummy;
    mbedtls_psa_mac_operation_t mbedtls_ctx;
} psa_driver_mac_context_t;
typedef union {
    unsigned dummy;
    mbedtls_psa_aead_operation_t mbedtls_ctx;
} psa_driver_aead_context_t;
typedef union {
    unsigned dummy;
    mbedtls_psa_sign_hash_interruptible_operation_t mbedtls_ctx;
} psa_driver_sign_hash_interruptible_context_t;
typedef union {
    unsigned dummy;
    mbedtls_psa_verify_hash_interruptible_operation_t mbedtls_ctx;
} psa_driver_verify_hash_interruptible_context_t;
typedef union {
    unsigned dummy;
    mbedtls_psa_pake_operation_t mbedtls_ctx;
} psa_driver_pake_context_t;
struct psa_mac_operation_s {
    unsigned int private_id;
    uint8_t private_mac_size;
    unsigned int private_is_sign : 1;
    psa_driver_mac_context_t private_ctx;
};
struct psa_aead_operation_s {
    unsigned int private_id;
    psa_algorithm_t private_alg;
    psa_key_type_t private_key_type;
    size_t private_ad_remaining;
    size_t private_body_remaining;
    unsigned int private_nonce_set : 1;
    unsigned int private_lengths_set : 1;
    unsigned int private_ad_started : 1;
    unsigned int private_body_started : 1;
    unsigned int private_is_encrypt : 1;
    psa_driver_aead_context_t private_ctx;
};
typedef struct {
    uint8_t *private_info;
    size_t private_info_length;
    uint8_t private_offset_in_block;
    uint8_t private_block_number;
    unsigned int private_state : 2;
    unsigned int private_info_set : 1;
    uint8_t private_output_block[64u];
    uint8_t private_prk[64u];
    struct psa_mac_operation_s private_hmac;
} psa_hkdf_key_derivation_t;
typedef struct {
    uint8_t private_data[32u];
} psa_tls12_ecjpake_to_pms_t;
typedef enum {
    PSA_TLS12_PRF_STATE_INIT,
    PSA_TLS12_PRF_STATE_SEED_SET,
    PSA_TLS12_PRF_STATE_OTHER_KEY_SET,
    PSA_TLS12_PRF_STATE_KEY_SET,
    PSA_TLS12_PRF_STATE_LABEL_SET,
    PSA_TLS12_PRF_STATE_OUTPUT
} psa_tls12_prf_key_derivation_state_t;
typedef struct psa_tls12_prf_key_derivation_s {
    uint8_t private_left_in_block;
    uint8_t private_block_number;
    psa_tls12_prf_key_derivation_state_t private_state;
    uint8_t *private_secret;
    size_t private_secret_length;
    uint8_t *private_seed;
    size_t private_seed_length;
    uint8_t *private_label;
    size_t private_label_length;
    uint8_t *private_other_secret;
    size_t private_other_secret_length;
    uint8_t private_Ai[64u];
    uint8_t private_output_block[64u];
} psa_tls12_prf_key_derivation_t;
typedef enum {
    PSA_PBKDF2_STATE_INIT,
    PSA_PBKDF2_STATE_INPUT_COST_SET,
    PSA_PBKDF2_STATE_SALT_SET,
    PSA_PBKDF2_STATE_PASSWORD_SET,
    PSA_PBKDF2_STATE_OUTPUT
} psa_pbkdf2_key_derivation_state_t;
typedef struct {
    psa_pbkdf2_key_derivation_state_t private_state;
    uint64_t private_input_cost;
    uint8_t *private_salt;
    size_t private_salt_length;
    uint8_t private_password[144u];
    size_t private_password_length;
    uint8_t private_output_block[64u];
    uint8_t private_bytes_used;
    uint32_t private_block_number;
} psa_pbkdf2_key_derivation_t;
typedef union {
    unsigned dummy;
    psa_hkdf_key_derivation_t private_hkdf;
    psa_tls12_prf_key_derivation_t private_tls12_prf;
    psa_tls12_ecjpake_to_pms_t private_tls12_ecjpake_to_pms;
    psa_pbkdf2_key_derivation_t private_pbkdf2;
} psa_driver_key_derivation_context_t;
struct psa_key_derivation_s {
    psa_algorithm_t private_alg;
    unsigned int private_can_output_key : 1;
    size_t private_capacity;
    psa_driver_key_derivation_context_t private_ctx;
};
struct psa_custom_key_parameters_s {
    uint32_t flags;
};
struct psa_key_policy_s {
    psa_key_usage_t private_usage;
    psa_algorithm_t private_alg;
    psa_algorithm_t private_alg2;
};
typedef struct psa_key_policy_s psa_key_policy_t;
typedef uint16_t psa_key_bits_t;
struct psa_key_attributes_s {
    psa_key_type_t private_type;
    psa_key_bits_t private_bits;
    psa_key_lifetime_t private_lifetime;
    psa_key_policy_t private_policy;
    mbedtls_svc_key_id_t private_id;
};
struct psa_sign_hash_interruptible_operation_s {
    unsigned int private_id;
    psa_driver_sign_hash_interruptible_context_t private_ctx;
    unsigned int private_error_occurred : 1;
    uint32_t private_num_ops;
};
struct psa_verify_hash_interruptible_operation_s {
    unsigned int private_id;
    psa_driver_verify_hash_interruptible_context_t private_ctx;
    unsigned int private_error_occurred : 1;
    uint32_t private_num_ops;
};
struct psa_key_agreement_iop_s {
    unsigned int private_id;
    mbedtls_psa_key_agreement_interruptible_operation_t private_mbedtls_ctx;
    uint32_t private_num_ops;
    psa_key_attributes_t private_attributes;
    unsigned int private_error_occurred : 1;
};
struct psa_generate_key_iop_s {
    unsigned int private_id;
    mbedtls_psa_generate_key_iop_t private_ctx;
    psa_key_attributes_t private_attributes;
    unsigned int private_error_occurred : 1;
    uint32_t private_num_ops;
};
struct psa_export_public_key_iop_s {
    unsigned int private_id;
    mbedtls_psa_export_public_key_iop_t private_ctx;
    unsigned int private_error_occurred : 1;
    uint32_t private_num_ops;
};
typedef struct psa_hash_operation_s psa_hash_operation_t;
typedef struct psa_xof_operation_s psa_xof_operation_t;
typedef struct psa_mac_operation_s psa_mac_operation_t;
typedef struct psa_cipher_operation_s psa_cipher_operation_t;
typedef struct psa_aead_operation_s psa_aead_operation_t;
typedef struct psa_key_derivation_s psa_key_derivation_operation_t;
typedef struct psa_sign_hash_interruptible_operation_s psa_sign_hash_interruptible_operation_t;
typedef struct psa_verify_hash_interruptible_operation_s psa_verify_hash_interruptible_operation_t;
typedef struct psa_key_agreement_iop_s psa_key_agreement_iop_t;
typedef struct psa_generate_key_iop_s psa_generate_key_iop_t;
typedef struct psa_export_public_key_iop_s psa_export_public_key_iop_t;
typedef struct mbedtls_psa_stats_s {
    size_t private_volatile_slots;
    size_t private_persistent_slots;
    size_t private_external_slots;
    size_t private_half_filled_slots;
    size_t private_cache_slots;
    size_t private_empty_slots;
    size_t private_locked_slots;
    psa_key_id_t private_max_open_internal_key_id;
    psa_key_id_t private_max_open_external_key_id;
} mbedtls_psa_stats_t;
typedef uint8_t psa_pake_role_t;
typedef uint8_t psa_pake_step_t;
typedef uint8_t psa_pake_primitive_type_t;
typedef uint8_t psa_pake_family_t;
typedef uint32_t psa_pake_primitive_t;
struct psa_pake_cipher_suite_s {
    psa_algorithm_t algorithm;
    psa_pake_primitive_type_t type;
    psa_pake_family_t family;
    uint16_t bits;
    uint32_t key_confirmation;
};
struct psa_crypto_driver_pake_inputs_s {
    uint8_t *private_password;
    size_t private_password_len;
    uint8_t *private_user;
    size_t private_user_len;
    uint8_t *private_peer;
    size_t private_peer_len;
    psa_key_attributes_t private_attributes;
    struct psa_pake_cipher_suite_s private_cipher_suite;
};
typedef enum psa_jpake_round {
    PSA_JPAKE_FIRST = 0,
    PSA_JPAKE_SECOND = 1,
    PSA_JPAKE_FINISHED = 2
} psa_jpake_round_t;
typedef enum psa_jpake_io_mode {
    PSA_JPAKE_INPUT = 0,
    PSA_JPAKE_OUTPUT = 1
} psa_jpake_io_mode_t;
struct psa_jpake_computation_stage_s {
    psa_jpake_round_t private_round;
    psa_jpake_io_mode_t private_io_mode;
    uint8_t private_inputs;
    uint8_t private_outputs;
    psa_pake_step_t private_step;
};
struct psa_pake_operation_s {
    unsigned int private_id;
    psa_algorithm_t private_alg;
    psa_pake_primitive_t private_primitive;
    uint8_t private_stage;
    union {
        uint8_t private_dummy;
        struct psa_jpake_computation_stage_s private_jpake;
    } private_computation_stage;
    union {
        psa_driver_pake_context_t private_ctx;
        struct psa_crypto_driver_pake_inputs_s private_inputs;
    } private_data;
};
typedef struct psa_pake_cipher_suite_s psa_pake_cipher_suite_t;
typedef struct psa_pake_operation_s psa_pake_operation_t;
typedef struct psa_crypto_driver_pake_inputs_s psa_crypto_driver_pake_inputs_t;
typedef enum {
    MBEDTLS_PK_SIGALG_NONE = 0,
    MBEDTLS_PK_SIGALG_RSA_PKCS1V15,
    MBEDTLS_PK_SIGALG_RSA_PSS,
    MBEDTLS_PK_SIGALG_ECDSA,
} mbedtls_pk_sigalg_t;
typedef struct mbedtls_pk_info_t mbedtls_pk_info_t;
typedef struct mbedtls_pk_context {
    const mbedtls_pk_info_t *private_pk_info;
    psa_key_type_t private_psa_type;
    mbedtls_svc_key_id_t private_priv_id;
    uint8_t private_pub_raw[(((4096u) / 8u + 5u) + 11u)];
    size_t private_pub_raw_len;
    size_t private_bits;
    psa_ecc_family_t private_ec_family;
} mbedtls_pk_context;
typedef void mbedtls_pk_restart_ctx;
typedef struct mbedtls_ssl_ciphersuite_t mbedtls_ssl_ciphersuite_t;
typedef struct mbedtls_asn1_buf {
    int tag;
    size_t len;
    unsigned char *p;
}
mbedtls_asn1_buf;
typedef struct mbedtls_asn1_bitstring {
    size_t len;
    unsigned char unused_bits;
    unsigned char *p;
}
mbedtls_asn1_bitstring;
typedef struct mbedtls_asn1_sequence {
    mbedtls_asn1_buf buf;
    struct mbedtls_asn1_sequence *next;
}
mbedtls_asn1_sequence;
typedef struct mbedtls_asn1_named_data {
    mbedtls_asn1_buf oid;
    mbedtls_asn1_buf val;
    struct mbedtls_asn1_named_data *next;
    unsigned char private_next_merged;
}
mbedtls_asn1_named_data;
typedef mbedtls_asn1_buf mbedtls_x509_buf;
typedef mbedtls_asn1_named_data mbedtls_x509_name;
typedef mbedtls_asn1_sequence mbedtls_x509_sequence;
typedef struct mbedtls_x509_authority {
    mbedtls_x509_buf keyIdentifier;
    mbedtls_x509_sequence authorityCertIssuer;
    mbedtls_x509_buf authorityCertSerialNumber;
    mbedtls_x509_buf raw;
}
mbedtls_x509_authority;
typedef struct mbedtls_x509_time {
    int year, mon, day;
    int hour, min, sec;
}
mbedtls_x509_time;
typedef struct mbedtls_x509_san_other_name {
    mbedtls_x509_buf type_id;
    union {
        struct {
            mbedtls_x509_buf oid;
            mbedtls_x509_buf val;
        }
        hardware_module_name;
    }
    value;
}
mbedtls_x509_san_other_name;
typedef struct mbedtls_x509_subject_alternative_name {
    int type;
    union {
        mbedtls_x509_san_other_name other_name;
        mbedtls_x509_name directory_name;
        mbedtls_x509_buf unstructured_name;
    }
    san;
}
mbedtls_x509_subject_alternative_name;
typedef struct mbedtls_x509_san_list {
    mbedtls_x509_subject_alternative_name node;
    struct mbedtls_x509_san_list *next;
}
mbedtls_x509_san_list;
typedef struct mbedtls_x509_crl_entry {
    mbedtls_x509_buf raw;
    mbedtls_x509_buf serial;
    mbedtls_x509_time revocation_date;
    mbedtls_x509_buf entry_ext;
    struct mbedtls_x509_crl_entry *next;
}
mbedtls_x509_crl_entry;
typedef struct mbedtls_x509_crl {
    mbedtls_x509_buf raw;
    mbedtls_x509_buf tbs;
    int version;
    mbedtls_x509_buf sig_oid;
    mbedtls_x509_buf issuer_raw;
    mbedtls_x509_name issuer;
    mbedtls_x509_time this_update;
    mbedtls_x509_time next_update;
    mbedtls_x509_crl_entry entry;
    mbedtls_x509_buf crl_ext;
    mbedtls_x509_buf private_sig_oid2;
    mbedtls_x509_buf private_sig;
    mbedtls_md_type_t private_sig_md;
    mbedtls_pk_sigalg_t private_sig_pk;
    struct mbedtls_x509_crl *next;
}
mbedtls_x509_crl;
typedef struct mbedtls_x509_crt {
    int private_own_buffer;
    mbedtls_x509_buf raw;
    mbedtls_x509_buf tbs;
    int version;
    mbedtls_x509_buf serial;
    mbedtls_x509_buf sig_oid;
    mbedtls_x509_buf issuer_raw;
    mbedtls_x509_buf subject_raw;
    mbedtls_x509_name issuer;
    mbedtls_x509_name subject;
    mbedtls_x509_time valid_from;
    mbedtls_x509_time valid_to;
    mbedtls_x509_buf pk_raw;
    mbedtls_pk_context pk;
    mbedtls_x509_buf issuer_id;
    mbedtls_x509_buf subject_id;
    mbedtls_x509_buf v3_ext;
    mbedtls_x509_sequence subject_alt_names;
    mbedtls_x509_buf subject_key_id;
    mbedtls_x509_authority authority_key_id;
    mbedtls_x509_sequence certificate_policies;
    int private_ext_types;
    int private_ca_istrue;
    int private_max_pathlen;
    unsigned int private_key_usage;
    mbedtls_x509_sequence ext_key_usage;
    unsigned char private_ns_cert_type;
    mbedtls_x509_buf private_sig;
    mbedtls_md_type_t private_sig_md;
    mbedtls_pk_sigalg_t private_sig_pk;
    union {
        size_t number;
        void *ptr;
    } private_unused;
    struct mbedtls_x509_crt *next;
}
mbedtls_x509_crt;
typedef struct mbedtls_x509_crt_profile {
    uint32_t allowed_mds;
    uint32_t allowed_pks;
    uint32_t allowed_curves;
    uint32_t rsa_min_bitlen;
}
mbedtls_x509_crt_profile;
typedef struct mbedtls_x509write_cert {
    int private_version;
    unsigned char private_serial[20];
    size_t private_serial_len;
    mbedtls_pk_context *private_subject_key;
    mbedtls_pk_context *private_issuer_key;
    mbedtls_asn1_named_data *private_subject;
    mbedtls_asn1_named_data *private_issuer;
    mbedtls_md_type_t private_md_alg;
    char private_not_before[15 + 1];
    char private_not_after[15 + 1];
    mbedtls_asn1_named_data *private_extensions;
}
mbedtls_x509write_cert;
typedef void mbedtls_x509_crt_restart_ctx;
typedef int (*mbedtls_x509_crt_ext_cb_t)(void *p_ctx,
                                         mbedtls_x509_crt const *crt,
                                         mbedtls_x509_buf const *oid,
                                         int critical,
                                         const unsigned char *p,
                                         const unsigned char *end);
typedef int mbedtls_ssl_send_t(void *ctx,
                               const unsigned char *buf,
                               size_t len);
typedef int mbedtls_ssl_recv_t(void *ctx,
                               unsigned char *buf,
                               size_t len);
typedef int mbedtls_ssl_recv_timeout_t(void *ctx,
                                       unsigned char *buf,
                                       size_t len,
                                       uint32_t timeout);
typedef void mbedtls_ssl_set_timer_t(void *ctx,
                                     uint32_t int_ms,
                                     uint32_t fin_ms);
typedef int mbedtls_ssl_get_timer_t(void *ctx);
typedef struct mbedtls_ssl_session mbedtls_ssl_session;
typedef struct mbedtls_ssl_context mbedtls_ssl_context;
typedef struct mbedtls_ssl_config mbedtls_ssl_config;
typedef int mbedtls_ssl_cache_get_t(void *data,
                                    unsigned char const *session_id,
                                    size_t session_id_len,
                                    mbedtls_ssl_session *session);
typedef int mbedtls_ssl_cache_set_t(void *data,
                                    unsigned char const *session_id,
                                    size_t session_id_len,
                                    const mbedtls_ssl_session *session);
typedef enum {
    MBEDTLS_SSL_TLS_PRF_NONE,
    MBEDTLS_SSL_TLS_PRF_SHA384,
    MBEDTLS_SSL_TLS_PRF_SHA256,
    MBEDTLS_SSL_HKDF_EXPAND_SHA384,
    MBEDTLS_SSL_HKDF_EXPAND_SHA256
}
mbedtls_tls_prf_types;
typedef enum {
    MBEDTLS_SSL_KEY_EXPORT_TLS12_MASTER_SECRET = 0,
    MBEDTLS_SSL_KEY_EXPORT_TLS1_3_CLIENT_EARLY_SECRET,
    MBEDTLS_SSL_KEY_EXPORT_TLS1_3_EARLY_EXPORTER_SECRET,
    MBEDTLS_SSL_KEY_EXPORT_TLS1_3_CLIENT_HANDSHAKE_TRAFFIC_SECRET,
    MBEDTLS_SSL_KEY_EXPORT_TLS1_3_SERVER_HANDSHAKE_TRAFFIC_SECRET,
    MBEDTLS_SSL_KEY_EXPORT_TLS1_3_CLIENT_APPLICATION_TRAFFIC_SECRET,
    MBEDTLS_SSL_KEY_EXPORT_TLS1_3_SERVER_APPLICATION_TRAFFIC_SECRET,
} mbedtls_ssl_key_export_type;
typedef void mbedtls_ssl_export_keys_t(void *p_expkey,
                                       mbedtls_ssl_key_export_type type,
                                       const unsigned char *secret,
                                       size_t secret_len,
                                       const unsigned char client_random[32],
                                       const unsigned char server_random[32],
                                       mbedtls_tls_prf_types tls_prf_type);
typedef int mbedtls_ssl_ticket_write_t(void *p_ticket,
                                       const mbedtls_ssl_session *session,
                                       unsigned char *start,
                                       const unsigned char *end,
                                       size_t *tlen,
                                       uint32_t *lifetime);
typedef int mbedtls_ssl_ticket_parse_t(void *p_ticket,
                                       mbedtls_ssl_session *session,
                                       unsigned char *buf,
                                       size_t len);
typedef int mbedtls_ssl_cookie_write_t(void *ctx,
                                       unsigned char **p, unsigned char *end,
                                       const unsigned char *info, size_t ilen);
typedef int mbedtls_ssl_cookie_check_t(void *ctx,
                                       const unsigned char *cookie, size_t clen,
                                       const unsigned char *info, size_t ilen);
typedef struct {
    uint16_t tls_id;
    const char *group_name;
    uint8_t is_supported;
} mbedtls_ssl_iana_tls_group_info_t;
typedef struct mbedtls_ssl_cache_context mbedtls_ssl_cache_context;
typedef struct mbedtls_ssl_ticket_key {
    unsigned char private_name[4];
    mbedtls_time_t private_generation_time;
    uint32_t private_lifetime;
    mbedtls_svc_key_id_t private_key;
    psa_algorithm_t private_alg;
    psa_key_type_t private_key_type;
    size_t private_key_bits;
}
mbedtls_ssl_ticket_key;
typedef struct mbedtls_ssl_ticket_context {
    mbedtls_ssl_ticket_key private_keys[2];
    unsigned char private_active;
    uint32_t private_ticket_lifetime;
}
mbedtls_ssl_ticket_context;
typedef struct mbedtls_ssl_cookie_ctx {
    mbedtls_svc_key_id_t private_psa_hmac_key;
    psa_algorithm_t private_psa_hmac_alg;
    unsigned long private_timeout;
} mbedtls_ssl_cookie_ctx;
typedef mbedtls_asn1_buf mbedtls_pkcs7_buf;
typedef struct mbedtls_pkcs7_signer_info {
    int private_version;
    mbedtls_x509_buf private_serial;
    mbedtls_x509_name private_issuer;
    mbedtls_x509_buf private_issuer_raw;
    mbedtls_x509_buf private_alg_identifier;
    mbedtls_x509_buf private_sig_alg_identifier;
    mbedtls_x509_buf private_sig;
    struct mbedtls_pkcs7_signer_info *private_next;
}
mbedtls_pkcs7_signer_info;
typedef struct mbedtls_pkcs7_signed_data {
    int private_version;
    mbedtls_pkcs7_buf private_digest_alg_identifiers;
    int private_no_of_certs;
    mbedtls_x509_crt private_certs;
    int private_no_of_crls;
    mbedtls_x509_crl private_crl;
    int private_no_of_signers;
    mbedtls_pkcs7_signer_info private_signers;
}
mbedtls_pkcs7_signed_data;
typedef struct mbedtls_pkcs7 {
    mbedtls_pkcs7_buf private_raw;
    mbedtls_pkcs7_signed_data private_signed_data;
}
mbedtls_pkcs7;
typedef struct mbedtls_x509_csr {
    mbedtls_x509_buf raw;
    mbedtls_x509_buf cri;
    int version;
    mbedtls_x509_buf subject_raw;
    mbedtls_x509_name subject;
    mbedtls_pk_context pk;
    unsigned int key_usage;
    unsigned char ns_cert_type;
    mbedtls_x509_sequence subject_alt_names;
    int private_ext_types;
    mbedtls_x509_buf sig_oid;
    mbedtls_x509_buf private_sig;
    mbedtls_md_type_t private_sig_md;
    mbedtls_pk_sigalg_t private_sig_pk;
}
mbedtls_x509_csr;
typedef struct mbedtls_x509write_csr {
    mbedtls_pk_context *private_key;
    mbedtls_asn1_named_data *private_subject;
    mbedtls_md_type_t private_md_alg;
    mbedtls_asn1_named_data *private_extensions;
}
mbedtls_x509write_csr;
typedef int (*mbedtls_x509_csr_ext_cb_t)(void *p_ctx,
                                         mbedtls_x509_csr const *csr,
                                         mbedtls_x509_buf const *oid,
                                         int critical,
                                         const unsigned char *p,
                                         const unsigned char *end);
typedef struct mbedtls_pem_context {
    unsigned char *private_buf;
    size_t private_buflen;
    unsigned char *private_info;
}
mbedtls_pem_context;
typedef enum {
    MBEDTLS_KW_MODE_KW = 0,
    MBEDTLS_KW_MODE_KWP = 1
} mbedtls_nist_kw_mode_t;
typedef enum {
    MBEDTLS_LMS_SHA256_M32_H10 = 0x6,
} mbedtls_lms_algorithm_type_t;
typedef enum {
    MBEDTLS_LMOTS_SHA256_N32_W8 = 4
} mbedtls_lmots_algorithm_type_t;
typedef struct {
    unsigned char private_I_key_identifier[(16u)];
    mbedtls_lmots_algorithm_type_t private_otstype;
    mbedtls_lms_algorithm_type_t private_type;
} mbedtls_lms_parameters_t;
typedef struct {
    mbedtls_lms_parameters_t private_params;
    unsigned char private_T_1_pub_key[32];
    unsigned char private_have_public_key;
} mbedtls_lms_public_t;
typedef uint32_t psa_driver_get_entropy_flags_t;
typedef struct mbedtls_platform_context {
    char private_dummy;
}
mbedtls_platform_context;
struct mbedtls_timing_hr_time {
    mbedtls_ms_time_t ms;
};
typedef struct mbedtls_timing_delay_context {
    struct mbedtls_timing_hr_time private_timer;
    uint32_t private_int_ms;
    uint32_t private_fin_ms;
} mbedtls_timing_delay_context;
typedef struct mbedtls_net_context {
    int fd;
}
mbedtls_net_context;

/* ============================================================
 * 平台适配层：统一动态库 API 与默认库名
 * ============================================================ */
#ifdef _WIN32
 #include <windows.h>
 typedef HMODULE mbedtls_lib_handle_t;
 static mbedtls_lib_handle_t mbedtls_load_lib(const char *path) { return LoadLibraryA(path); }
 static void *mbedtls_get_sym(mbedtls_lib_handle_t h, const char *name) {
     return (void *)GetProcAddress(h, name);
 }
 static void mbedtls_free_lib(mbedtls_lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *mbedtls_lib_error(void) { return "LoadLibrary failed"; }
 #define MBEDTLS_PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *mbedtls_lib_handle_t;
 static mbedtls_lib_handle_t mbedtls_load_lib(const char *path) {
     return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
 }
 static void *mbedtls_get_sym(mbedtls_lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void mbedtls_free_lib(mbedtls_lib_handle_t h) { if (h) dlclose(h); }
 static const char *mbedtls_lib_error(void) { return dlerror(); }
 #define MBEDTLS_PATH_SEP ':'
#endif
/* ---------- 数据符号副本 ---------- */
void *mbedtls_cipher_base_lookup_table = NULL;
void *mbedtls_cipher_definitions = NULL;
void *mbedtls_cipher_supported = NULL;
void *mbedtls_ecdsa_info = NULL;
void *mbedtls_ecdsa_opaque_info = NULL;
void *mbedtls_eckey_info = NULL;
void *mbedtls_eckeydh_info = NULL;
char *mbedtls_platform_dev_random = {0};
void *mbedtls_rsa_info = NULL;
void *mbedtls_rsa_opaque_info = NULL;
mbedtls_ssl_iana_tls_group_info_t *mbedtls_ssl_iana_tls_group_info = NULL;
void *mbedtls_ssl_tls13_hello_retry_request_magic = NULL;
void *mbedtls_ssl_tls13_labels = NULL;
mbedtls_x509_crt_profile mbedtls_x509_crt_profile_default = {0};
mbedtls_x509_crt_profile mbedtls_x509_crt_profile_next = {0};
mbedtls_x509_crt_profile mbedtls_x509_crt_profile_none = {0};
mbedtls_x509_crt_profile mbedtls_x509_crt_profile_suiteb = {0};
void *psa_to_lms_errors = NULL;
void *psa_to_md_errors = NULL;
void *psa_to_pk_ecdsa_errors = NULL;
void *psa_to_pk_rsa_errors = NULL;
void *psa_to_ssl_errors = NULL;

/* 前向声明（LOADER 在下方定义 LIB_sym） */
void *mbedtls_sym(const char *name);

void mbedtls_dyn_sync_globals(void) {
    void *p;
    (void)p;
    if ((p = mbedtls_sym("mbedtls_cipher_base_lookup_table")) != NULL) mbedtls_cipher_base_lookup_table = p;
    if ((p = mbedtls_sym("mbedtls_cipher_definitions")) != NULL) mbedtls_cipher_definitions = p;
    if ((p = mbedtls_sym("mbedtls_cipher_supported")) != NULL) mbedtls_cipher_supported = p;
    if ((p = mbedtls_sym("mbedtls_ecdsa_info")) != NULL) mbedtls_ecdsa_info = p;
    if ((p = mbedtls_sym("mbedtls_ecdsa_opaque_info")) != NULL) mbedtls_ecdsa_opaque_info = p;
    if ((p = mbedtls_sym("mbedtls_eckey_info")) != NULL) mbedtls_eckey_info = p;
    if ((p = mbedtls_sym("mbedtls_eckeydh_info")) != NULL) mbedtls_eckeydh_info = p;
    if ((p = mbedtls_sym("mbedtls_platform_dev_random")) != NULL) memcpy(&mbedtls_platform_dev_random, p, sizeof(mbedtls_platform_dev_random));
    if ((p = mbedtls_sym("mbedtls_rsa_info")) != NULL) mbedtls_rsa_info = p;
    if ((p = mbedtls_sym("mbedtls_rsa_opaque_info")) != NULL) mbedtls_rsa_opaque_info = p;
    if ((p = mbedtls_sym("mbedtls_ssl_iana_tls_group_info")) != NULL) mbedtls_ssl_iana_tls_group_info = p;
    if ((p = mbedtls_sym("mbedtls_ssl_tls13_hello_retry_request_magic")) != NULL) mbedtls_ssl_tls13_hello_retry_request_magic = p;
    if ((p = mbedtls_sym("mbedtls_ssl_tls13_labels")) != NULL) mbedtls_ssl_tls13_labels = p;
    if ((p = mbedtls_sym("mbedtls_x509_crt_profile_default")) != NULL) memcpy(&mbedtls_x509_crt_profile_default, p, sizeof(mbedtls_x509_crt_profile_default));
    if ((p = mbedtls_sym("mbedtls_x509_crt_profile_next")) != NULL) memcpy(&mbedtls_x509_crt_profile_next, p, sizeof(mbedtls_x509_crt_profile_next));
    if ((p = mbedtls_sym("mbedtls_x509_crt_profile_none")) != NULL) memcpy(&mbedtls_x509_crt_profile_none, p, sizeof(mbedtls_x509_crt_profile_none));
    if ((p = mbedtls_sym("mbedtls_x509_crt_profile_suiteb")) != NULL) memcpy(&mbedtls_x509_crt_profile_suiteb, p, sizeof(mbedtls_x509_crt_profile_suiteb));
    if ((p = mbedtls_sym("psa_to_lms_errors")) != NULL) psa_to_lms_errors = p;
    if ((p = mbedtls_sym("psa_to_md_errors")) != NULL) psa_to_md_errors = p;
    if ((p = mbedtls_sym("psa_to_pk_ecdsa_errors")) != NULL) psa_to_pk_ecdsa_errors = p;
    if ((p = mbedtls_sym("psa_to_pk_rsa_errors")) != NULL) psa_to_pk_rsa_errors = p;
    if ((p = mbedtls_sym("psa_to_ssl_errors")) != NULL) psa_to_ssl_errors = p;
}

/* ============================================================
 * 全局状态与库加载
 * ============================================================ */
static mbedtls_lib_handle_t mbedtls_handle = NULL;
static int mbedtls_init_state = 0;

#define MBEDTLS_MAX_CANDIDATES 64
#define MBEDTLS_PATH_BUF_LEN   1024

static const char *mbedtls_defaults[] = {
"libmbedtls.so.23", "libmbedtls.so.14", "libmbedtls.so", "libmbedtls.dylib", "libmbedtls.dll"
};

static int mbedtls_do_load(const char *library_path) {
    if (mbedtls_handle) return 0;
    static char bufs[MBEDTLS_MAX_CANDIDATES][MBEDTLS_PATH_BUF_LEN];
    const char *candidates[MBEDTLS_MAX_CANDIDATES];
    int n = 0, i;

    if (library_path && *library_path && n < MBEDTLS_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], MBEDTLS_PATH_BUF_LEN, "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    const char *explicit_lib = getenv("MBEDTLS_DYN_LIB");
    if (explicit_lib && *explicit_lib && n < MBEDTLS_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], MBEDTLS_PATH_BUF_LEN, "%s", explicit_lib);
        candidates[n] = bufs[n]; n++;
    }
    const char *env_path = getenv(
#ifdef _WIN32
        "PATH"
#elif defined(__APPLE__)
        "DYLD_mbedtlsRARY_PATH"
#else
        "LD_mbedtlsRARY_PATH"
#endif
    );
    if (env_path && *env_path) {
        char *copy = strdup(env_path);
        if (copy) {
            char *p = copy;
            while (p && *p && n < MBEDTLS_MAX_CANDIDATES - 2) {
                char *sep = strchr(p, MBEDTLS_PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    for (i = 0; i < (int)(sizeof(mbedtls_defaults)/sizeof(mbedtls_defaults[0])); i++) {
                        if (n >= MBEDTLS_MAX_CANDIDATES - 2) break;
                        snprintf(bufs[n], MBEDTLS_PATH_BUF_LEN, "%s/%s", p, mbedtls_defaults[i]);
                        candidates[n] = bufs[n]; n++;
                    }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }
    for (i = 0; i < (int)(sizeof(mbedtls_defaults)/sizeof(mbedtls_defaults[0])) && n < MBEDTLS_MAX_CANDIDATES - 1; i++)
        candidates[n++] = mbedtls_defaults[i];
    const char *home = getenv("HOME");
    if (home && *home && n < MBEDTLS_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], MBEDTLS_PATH_BUF_LEN, "%s/.nix-profile/lib/%s", home, mbedtls_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    if (n < MBEDTLS_MAX_CANDIDATES - 1) {
        snprintf(bufs[n], MBEDTLS_PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s", mbedtls_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        mbedtls_handle = mbedtls_load_lib(candidates[i]);
        if (mbedtls_handle) break;
    }
    if (!mbedtls_handle) {
        fprintf(stderr, "mbedtls_dyn: cannot load library (%s)\n", mbedtls_lib_error());
        return -1;
    }
    if (!mbedtls_get_sym(mbedtls_handle, "mbedtls_aes_cmac_prf_128")) {
        fprintf(stderr, "mbedtls_dyn: loaded library does not export %s\n", "mbedtls_aes_cmac_prf_128");
        mbedtls_free_lib(mbedtls_handle); mbedtls_handle = NULL;
        return -1;
    }
    fprintf(stderr, "mbedtls_dyn: loaded %d %s\n", i, candidates[i]);
    mbedtls_dyn_sync_globals();
    return 0;
}

void *mbedtls_sym(const char *name) { return mbedtls_handle ? mbedtls_get_sym(mbedtls_handle, name) : NULL; }

int mbedtls_libok(void) {
    if (mbedtls_init_state == 0)
        mbedtls_init_state = (mbedtls_do_load(NULL) == 0) ? 1 : -1;
    return (mbedtls_init_state == 1 && mbedtls_handle != NULL) ? 1 : 0;
}

int mbedtls_dyn_init(const char *library_path) {
    if (mbedtls_do_load(library_path) == 0) { mbedtls_init_state = 1; return 0; }
    if (mbedtls_init_state == 0) mbedtls_init_state = -1;
    return -1;
}

void mbedtls_dyn_cleanup(void) {
    /* 不卸载已成功加载的库（dlclose 不安全，且函数指针可能仍被持有） */
    if (!mbedtls_handle) mbedtls_init_state = 0;
    else if (mbedtls_init_state == -1) mbedtls_init_state = 0;
}

int mbedtls_dyn_has_symbol(const char *name) {
    if (!mbedtls_libok()) return 0;
    return mbedtls_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏：首次调用 dlsym 并缓存；失败打印 stderr 并返回安全值
 * ============================================================ */
#define MBEDTLS_WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!mbedtls_libok()) return fallback;                                \
        *(void **)(&fn) = mbedtls_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "mbedtls_dyn: dlsym failed: %s\n", #name);        \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define MBEDTLS_WRAP_VOID(name, params, args)                                     \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!mbedtls_libok()) return;                                         \
        *(void **)(&fn) = mbedtls_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "mbedtls_dyn: dlsym failed: %s\n", #name);        \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}
/* ---------- 全量函数封装 ---------- */
MBEDTLS_WRAP_RET(const mbedtls_asn1_named_data *, mbedtls_asn1_find_named_data, (const mbedtls_asn1_named_data * a0, const char * a1, size_t a2), (a0, a1, a2), NULL)
MBEDTLS_WRAP_VOID(mbedtls_asn1_free_named_data_list, (mbedtls_asn1_named_data ** a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_asn1_free_named_data_list_shallow, (mbedtls_asn1_named_data * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_asn1_get_alg, (unsigned char ** a0, const unsigned char * a1, mbedtls_asn1_buf * a2, mbedtls_asn1_buf * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_get_alg_null, (unsigned char ** a0, const unsigned char * a1, mbedtls_asn1_buf * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_get_bitstring, (unsigned char ** a0, const unsigned char * a1, mbedtls_asn1_bitstring * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_get_bitstring_null, (unsigned char ** a0, const unsigned char * a1, size_t * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_get_bool, (unsigned char ** a0, const unsigned char * a1, int * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_get_enum, (unsigned char ** a0, const unsigned char * a1, int * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_get_int, (unsigned char ** a0, const unsigned char * a1, int * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_get_integer, (unsigned char ** a0, const unsigned char * a1, unsigned char ** a2, size_t * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_get_len, (unsigned char ** a0, const unsigned char * a1, size_t * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_get_sequence_of, (unsigned char ** a0, const unsigned char * a1, mbedtls_asn1_sequence * a2, int a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_get_tag, (unsigned char ** a0, const unsigned char * a1, size_t * a2, int a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_asn1_sequence_free, (mbedtls_asn1_sequence * a0), (a0))
MBEDTLS_WRAP_RET(mbedtls_asn1_named_data *, mbedtls_asn1_store_named_data, (mbedtls_asn1_named_data ** a0, const char * a1, size_t a2, const unsigned char * a3, size_t a4), (a0, a1, a2, a3, a4), NULL)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_traverse_sequence_of, (unsigned char ** a0, const unsigned char * a1, unsigned char a2, unsigned char a3, unsigned char a4, unsigned char a5, int (*a6 ) (void *, int, unsigned char *, size_t), void * a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_algorithm_identifier, (unsigned char ** a0, const unsigned char * a1, const char * a2, size_t a3, size_t a4), (a0, a1, a2, a3, a4), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_algorithm_identifier_ext, (unsigned char ** a0, const unsigned char * a1, const char * a2, size_t a3, size_t a4, int a5), (a0, a1, a2, a3, a4, a5), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_bitstring, (unsigned char ** a0, const unsigned char * a1, const unsigned char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_bool, (unsigned char ** a0, const unsigned char * a1, int a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_enum, (unsigned char ** a0, const unsigned char * a1, int a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_ia5_string, (unsigned char ** a0, const unsigned char * a1, const char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_int, (unsigned char ** a0, const unsigned char * a1, int a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_integer, (unsigned char ** a0, unsigned char * a1, const unsigned char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_len, (unsigned char ** a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_mpi, (unsigned char ** a0, const unsigned char * a1, const mbedtls_mpi * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_named_bitstring, (unsigned char ** a0, const unsigned char * a1, const unsigned char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_null, (unsigned char ** a0, const unsigned char * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_octet_string, (unsigned char ** a0, const unsigned char * a1, const unsigned char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_oid, (unsigned char ** a0, const unsigned char * a1, const char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_printable_string, (unsigned char ** a0, const unsigned char * a1, const char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_raw_buffer, (unsigned char ** a0, const unsigned char * a1, const unsigned char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_tag, (unsigned char ** a0, const unsigned char * a1, unsigned char a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_tagged_string, (unsigned char ** a0, const unsigned char * a1, int a2, const char * a3, size_t a4), (a0, a1, a2, a3, a4), -1)
MBEDTLS_WRAP_RET(int, mbedtls_asn1_write_utf8_string, (unsigned char ** a0, const unsigned char * a1, const char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_base64_decode, (unsigned char * a0, size_t a1, size_t * a2, const unsigned char * a3, size_t a4), (a0, a1, a2, a3, a4), -1)
MBEDTLS_WRAP_RET(int, mbedtls_base64_encode, (unsigned char * a0, size_t a1, size_t * a2, const unsigned char * a3, size_t a4), (a0, a1, a2, a3, a4), -1)
MBEDTLS_WRAP_RET(int, mbedtls_base64_self_test, (int a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ct_memcmp, (const void * a0, const void * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_debug_set_threshold, (int a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_ecdsa_der_to_raw, (size_t a0, const unsigned char * a1, size_t a2, unsigned char * a3, size_t a4, size_t * a5), (a0, a1, a2, a3, a4, a5), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ecdsa_raw_to_der, (size_t a0, const unsigned char * a1, size_t a2, unsigned char * a3, size_t a4, size_t * a5), (a0, a1, a2, a3, a4, a5), -1)
MBEDTLS_WRAP_RET(int, mbedtls_lms_export_public_key, (const mbedtls_lms_public_t * a0, unsigned char * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_lms_import_public_key, (mbedtls_lms_public_t * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_lms_public_free, (mbedtls_lms_public_t * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_lms_public_init, (mbedtls_lms_public_t * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_lms_verify, (const mbedtls_lms_public_t * a0, const unsigned char * a1, size_t a2, const unsigned char * a3, size_t a4), (a0, a1, a2, a3, a4), -1)
MBEDTLS_WRAP_RET(int, mbedtls_md, (const mbedtls_md_info_t * a0, const unsigned char * a1, size_t a2, unsigned char * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_md_clone, (mbedtls_md_context_t * a0, const mbedtls_md_context_t * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_md_finish, (mbedtls_md_context_t * a0, unsigned char * a1), (a0, a1), -1)
MBEDTLS_WRAP_VOID(mbedtls_md_free, (mbedtls_md_context_t * a0), (a0))
MBEDTLS_WRAP_RET(unsigned char, mbedtls_md_get_size, (const mbedtls_md_info_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(mbedtls_md_type_t, mbedtls_md_get_type, (const mbedtls_md_info_t * a0), (a0), ((mbedtls_md_type_t)0))
MBEDTLS_WRAP_RET(const mbedtls_md_info_t *, mbedtls_md_info_from_type, (mbedtls_md_type_t a0), (a0), NULL)
MBEDTLS_WRAP_VOID(mbedtls_md_init, (mbedtls_md_context_t * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_md_setup, (mbedtls_md_context_t * a0, const mbedtls_md_info_t * a1, int a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_md_starts, (mbedtls_md_context_t * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_md_update, (mbedtls_md_context_t * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(mbedtls_ms_time_t, mbedtls_ms_time, (), (), ((mbedtls_ms_time_t)0))
MBEDTLS_WRAP_RET(int, mbedtls_net_accept, (mbedtls_net_context * a0, mbedtls_net_context * a1, void * a2, size_t a3, size_t * a4), (a0, a1, a2, a3, a4), -1)
MBEDTLS_WRAP_RET(int, mbedtls_net_bind, (mbedtls_net_context * a0, const char * a1, const char * a2, int a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_net_close, (mbedtls_net_context * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_net_connect, (mbedtls_net_context * a0, const char * a1, const char * a2, int a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_net_free, (mbedtls_net_context * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_net_init, (mbedtls_net_context * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_net_poll, (mbedtls_net_context * a0, uint32_t a1, uint32_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_net_recv, (void * a0, unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_net_recv_timeout, (void * a0, unsigned char * a1, size_t a2, uint32_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_net_send, (void * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_net_set_block, (mbedtls_net_context * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_net_set_nonblock, (mbedtls_net_context * a0), (a0), -1)
MBEDTLS_WRAP_VOID(mbedtls_net_usleep, (long unsigned int a0), (a0))
MBEDTLS_WRAP_RET(psa_status_t, mbedtls_nist_kw_unwrap, (mbedtls_svc_key_id_t a0, mbedtls_nist_kw_mode_t a1, const unsigned char * a2, size_t a3, unsigned char * a4, size_t a5, size_t * a6), (a0, a1, a2, a3, a4, a5, a6), 0)
MBEDTLS_WRAP_RET(psa_status_t, mbedtls_nist_kw_wrap, (mbedtls_svc_key_id_t a0, mbedtls_nist_kw_mode_t a1, const unsigned char * a2, size_t a3, unsigned char * a4, size_t a5, size_t * a6), (a0, a1, a2, a3, a4, a5, a6), 0)
MBEDTLS_WRAP_RET(int, mbedtls_oid_from_numeric_string, (mbedtls_asn1_buf * a0, const char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_oid_get_numeric_string, (char * a0, size_t a1, const mbedtls_asn1_buf * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_pem_free, (mbedtls_pem_context * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_pem_init, (mbedtls_pem_context * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_pem_read_buffer, (mbedtls_pem_context * a0, const char * a1, const char * a2, const unsigned char * a3, const unsigned char * a4, size_t a5, size_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pem_write_buffer, (const char * a0, const char * a1, const unsigned char * a2, size_t a3, unsigned char * a4, size_t a5, size_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_can_do_psa, (const mbedtls_pk_context * a0, psa_algorithm_t a1, psa_key_usage_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_check_pair, (const mbedtls_pk_context * a0, const mbedtls_pk_context * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_copy_from_psa, (mbedtls_svc_key_id_t a0, mbedtls_pk_context * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_copy_public_from_psa, (mbedtls_svc_key_id_t a0, mbedtls_pk_context * a1), (a0, a1), -1)
MBEDTLS_WRAP_VOID(mbedtls_pk_free, (mbedtls_pk_context * a0), (a0))
MBEDTLS_WRAP_RET(size_t, mbedtls_pk_get_bitlen, (const mbedtls_pk_context * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_key_type_t, mbedtls_pk_get_key_type, (const mbedtls_pk_context * a0), (a0), ((psa_key_type_t)0))
MBEDTLS_WRAP_RET(int, mbedtls_pk_get_psa_attributes, (const mbedtls_pk_context * a0, psa_key_usage_t a1, psa_key_attributes_t * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_import_into_psa, (const mbedtls_pk_context * a0, const psa_key_attributes_t * a1, mbedtls_svc_key_id_t * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_pk_init, (mbedtls_pk_context * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_pk_parse_key, (mbedtls_pk_context * a0, const unsigned char * a1, size_t a2, const unsigned char * a3, size_t a4), (a0, a1, a2, a3, a4), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_parse_keyfile, (mbedtls_pk_context * a0, const char * a1, const char * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_parse_public_key, (mbedtls_pk_context * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_parse_public_keyfile, (mbedtls_pk_context * a0, const char * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_sign, (mbedtls_pk_context * a0, mbedtls_md_type_t a1, const unsigned char * a2, size_t a3, unsigned char * a4, size_t a5, size_t * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_sign_ext, (mbedtls_pk_sigalg_t a0, mbedtls_pk_context * a1, mbedtls_md_type_t a2, const unsigned char * a3, size_t a4, unsigned char * a5, size_t a6, size_t * a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_sign_restartable, (mbedtls_pk_context * a0, mbedtls_md_type_t a1, const unsigned char * a2, size_t a3, unsigned char * a4, size_t a5, size_t * a6, mbedtls_pk_restart_ctx * a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_verify, (mbedtls_pk_context * a0, mbedtls_md_type_t a1, const unsigned char * a2, size_t a3, const unsigned char * a4, size_t a5), (a0, a1, a2, a3, a4, a5), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_verify_ext, (mbedtls_pk_sigalg_t a0, mbedtls_pk_context * a1, mbedtls_md_type_t a2, const unsigned char * a3, size_t a4, const unsigned char * a5, size_t a6), (a0, a1, a2, a3, a4, a5, a6), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_verify_restartable, (mbedtls_pk_context * a0, mbedtls_md_type_t a1, const unsigned char * a2, size_t a3, const unsigned char * a4, size_t a5, mbedtls_pk_restart_ctx * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_wrap_psa, (mbedtls_pk_context * a0, const mbedtls_svc_key_id_t a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_write_key_der, (const mbedtls_pk_context * a0, unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_write_key_pem, (const mbedtls_pk_context * a0, unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_write_pubkey_der, (const mbedtls_pk_context * a0, unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_write_pubkey_pem, (const mbedtls_pk_context * a0, unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pk_write_pubkey_psa, (const mbedtls_pk_context * a0, unsigned char * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_pkcs7_free, (mbedtls_pkcs7 * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_pkcs7_init, (mbedtls_pkcs7 * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_pkcs7_parse_der, (mbedtls_pkcs7 * a0, const unsigned char * a1, const size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pkcs7_signed_data_verify, (mbedtls_pkcs7 * a0, const mbedtls_x509_crt * a1, const unsigned char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_pkcs7_signed_hash_verify, (mbedtls_pkcs7 * a0, const mbedtls_x509_crt * a1, const unsigned char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_platform_get_entropy, (psa_driver_get_entropy_flags_t a0, size_t * a1, unsigned char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(struct tm *, mbedtls_platform_gmtime_r, (const mbedtls_time_t * a0, struct tm * a1), (a0, a1), NULL)
MBEDTLS_WRAP_RET(int, mbedtls_platform_setup, (mbedtls_platform_context * a0), (a0), -1)
MBEDTLS_WRAP_VOID(mbedtls_platform_teardown, (mbedtls_platform_context * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_platform_zeroize, (void * a0, size_t a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_psa_crypto_free, (), ())
MBEDTLS_WRAP_RET(int, mbedtls_psa_get_random, (void * a0, unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_psa_get_stats, (mbedtls_psa_stats_t * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_ssl_cache_free, (mbedtls_ssl_cache_context * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_cache_get, (void * a0, const unsigned char * a1, size_t a2, mbedtls_ssl_session * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_cache_init, (mbedtls_ssl_cache_context * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_cache_remove, (void * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_cache_set, (void * a0, const unsigned char * a1, size_t a2, const mbedtls_ssl_session * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_cache_set_max_entries, (mbedtls_ssl_cache_context * a0, int a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_cache_set_timeout, (mbedtls_ssl_cache_context * a0, int a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_check_pending, (const mbedtls_ssl_context * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_check_record, (const mbedtls_ssl_context * a0, unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(const mbedtls_ssl_ciphersuite_t *, mbedtls_ssl_ciphersuite_from_id, (int a0), (a0), NULL)
MBEDTLS_WRAP_RET(const mbedtls_ssl_ciphersuite_t *, mbedtls_ssl_ciphersuite_from_string, (const char * a0), (a0), NULL)
MBEDTLS_WRAP_RET(size_t, mbedtls_ssl_ciphersuite_get_cipher_key_bitlen, (const mbedtls_ssl_ciphersuite_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_close_notify, (mbedtls_ssl_context * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_conf_alpn_protocols, (mbedtls_ssl_config * a0, const char *const * a1), (a0, a1), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_authmode, (mbedtls_ssl_config * a0, int a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_ca_chain, (mbedtls_ssl_config * a0, mbedtls_x509_crt * a1, mbedtls_x509_crl * a2), (a0, a1, a2))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_cert_profile, (mbedtls_ssl_config * a0, const mbedtls_x509_crt_profile * a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_cert_req_ca_list, (mbedtls_ssl_config * a0, char a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_conf_cid, (mbedtls_ssl_config * a0, size_t a1, int a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_ciphersuites, (mbedtls_ssl_config * a0, const int * a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_dbg, (mbedtls_ssl_config * a0, void (*a1 ) (void *, int, const char *, int, const char *), void * a2), (a0, a1, a2))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_dtls_anti_replay, (mbedtls_ssl_config * a0, char a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_dtls_badmac_limit, (mbedtls_ssl_config * a0, unsigned int a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_dtls_cookies, (mbedtls_ssl_config * a0, mbedtls_ssl_cookie_write_t (*a1), mbedtls_ssl_cookie_check_t (*a2), void * a3), (a0, a1, a2, a3))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_encrypt_then_mac, (mbedtls_ssl_config * a0, char a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_endpoint, (mbedtls_ssl_config * a0, int a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_extended_master_secret, (mbedtls_ssl_config * a0, char a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_groups, (mbedtls_ssl_config * a0, const uint16_t * a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_handshake_timeout, (mbedtls_ssl_config * a0, uint32_t a1, uint32_t a2), (a0, a1, a2))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_legacy_renegotiation, (mbedtls_ssl_config * a0, int a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_conf_max_frag_len, (mbedtls_ssl_config * a0, unsigned char a1), (a0, a1), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_new_session_tickets, (mbedtls_ssl_config * a0, uint16_t a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_conf_own_cert, (mbedtls_ssl_config * a0, mbedtls_x509_crt * a1, mbedtls_pk_context * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_preference_order, (mbedtls_ssl_config * a0, int a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_conf_psk, (mbedtls_ssl_config * a0, const unsigned char * a1, size_t a2, const unsigned char * a3, size_t a4), (a0, a1, a2, a3, a4), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_psk_cb, (mbedtls_ssl_config * a0, int (*a1 ) (void *, mbedtls_ssl_context *, const unsigned char *, size_t), void * a2), (a0, a1, a2))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_conf_psk_opaque, (mbedtls_ssl_config * a0, mbedtls_svc_key_id_t a1, const unsigned char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_read_timeout, (mbedtls_ssl_config * a0, uint32_t a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_renegotiation, (mbedtls_ssl_config * a0, int a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_renegotiation_enforced, (mbedtls_ssl_config * a0, int a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_renegotiation_period, (mbedtls_ssl_config * a0, const unsigned char * a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_session_cache, (mbedtls_ssl_config * a0, void * a1, mbedtls_ssl_cache_get_t (*a2), mbedtls_ssl_cache_set_t (*a3)), (a0, a1, a2, a3))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_session_tickets, (mbedtls_ssl_config * a0, int a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_session_tickets_cb, (mbedtls_ssl_config * a0, mbedtls_ssl_ticket_write_t (*a1), mbedtls_ssl_ticket_parse_t (*a2), void * a3), (a0, a1, a2, a3))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_sig_algs, (mbedtls_ssl_config * a0, const uint16_t * a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_sni, (mbedtls_ssl_config * a0, int (*a1 ) (void *, mbedtls_ssl_context *, const unsigned char *, size_t), void * a2), (a0, a1, a2))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_tls13_key_exchange_modes, (mbedtls_ssl_config * a0, const int a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_transport, (mbedtls_ssl_config * a0, int a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_conf_verify, (mbedtls_ssl_config * a0, int (*a1 ) (void *, mbedtls_x509_crt *, int, uint32_t *), void * a2), (a0, a1, a2))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_config_defaults, (mbedtls_ssl_config * a0, int a1, int a2, int a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_config_free, (mbedtls_ssl_config * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_ssl_config_init, (mbedtls_ssl_config * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_context_load, (mbedtls_ssl_context * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_context_save, (mbedtls_ssl_context * a0, unsigned char * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_cookie_free, (mbedtls_ssl_cookie_ctx * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_ssl_cookie_init, (mbedtls_ssl_cookie_ctx * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_ssl_cookie_set_timeout, (mbedtls_ssl_cookie_ctx * a0, long unsigned int a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_cookie_setup, (mbedtls_ssl_cookie_ctx * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_export_keying_material, (mbedtls_ssl_context * a0, uint8_t * a1, const size_t a2, const char * a3, const size_t a4, const unsigned char * a5, const size_t a6, const int a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_free, (mbedtls_ssl_context * a0), (a0))
MBEDTLS_WRAP_RET(const char *, mbedtls_ssl_get_alpn_protocol, (const mbedtls_ssl_context * a0), (a0), NULL)
MBEDTLS_WRAP_RET(size_t, mbedtls_ssl_get_bytes_avail, (const mbedtls_ssl_context * a0), (a0), 0)
MBEDTLS_WRAP_RET(const char *, mbedtls_ssl_get_ciphersuite, (const mbedtls_ssl_context * a0), (a0), NULL)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_get_ciphersuite_id, (const char * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_get_ciphersuite_id_from_ssl, (const mbedtls_ssl_context * a0), (a0), -1)
MBEDTLS_WRAP_RET(const char *, mbedtls_ssl_get_ciphersuite_name, (const int a0), (a0), NULL)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_get_fatal_alert, (const mbedtls_ssl_context * a0), (a0), -1)
MBEDTLS_WRAP_RET(const unsigned char *, mbedtls_ssl_get_hs_sni, (mbedtls_ssl_context * a0, size_t * a1), (a0, a1), NULL)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_get_max_in_record_payload, (const mbedtls_ssl_context * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_get_max_out_record_payload, (const mbedtls_ssl_context * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_get_own_cid, (mbedtls_ssl_context * a0, int * a1, unsigned char * a2, size_t * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(const mbedtls_x509_crt *, mbedtls_ssl_get_peer_cert, (const mbedtls_ssl_context * a0), (a0), NULL)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_get_peer_cid, (mbedtls_ssl_context * a0, int * a1, unsigned char * a2, size_t * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_get_record_expansion, (const mbedtls_ssl_context * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_get_session, (const mbedtls_ssl_context * a0, mbedtls_ssl_session * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(const uint16_t *, mbedtls_ssl_get_supported_group_list, (), (), NULL)
MBEDTLS_WRAP_RET(uint32_t, mbedtls_ssl_get_verify_result, (const mbedtls_ssl_context * a0), (a0), 0)
MBEDTLS_WRAP_RET(const char *, mbedtls_ssl_get_version, (const mbedtls_ssl_context * a0), (a0), NULL)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_handshake, (mbedtls_ssl_context * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_handshake_step, (mbedtls_ssl_context * a0), (a0), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_init, (mbedtls_ssl_context * a0), (a0))
MBEDTLS_WRAP_RET(const int *, mbedtls_ssl_list_ciphersuites, (), (), NULL)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_read, (mbedtls_ssl_context * a0, unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_renegotiate, (mbedtls_ssl_context * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_send_alert_message, (mbedtls_ssl_context * a0, unsigned char a1, unsigned char a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_session_free, (mbedtls_ssl_session * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_ssl_session_init, (mbedtls_ssl_session * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_session_load, (mbedtls_ssl_session * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_session_reset, (mbedtls_ssl_context * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_session_save, (const mbedtls_ssl_session * a0, unsigned char * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_set_bio, (mbedtls_ssl_context * a0, void * a1, mbedtls_ssl_send_t (*a2), mbedtls_ssl_recv_t (*a3), mbedtls_ssl_recv_timeout_t (*a4)), (a0, a1, a2, a3, a4))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_set_cid, (mbedtls_ssl_context * a0, int a1, const unsigned char * a2, size_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_set_client_transport_id, (mbedtls_ssl_context * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_set_datagram_packing, (mbedtls_ssl_context * a0, unsigned int a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_set_export_keys_cb, (mbedtls_ssl_context * a0, mbedtls_ssl_export_keys_t (*a1), void * a2), (a0, a1, a2))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_set_hostname, (mbedtls_ssl_context * a0, const char * a1), (a0, a1), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_set_hs_authmode, (mbedtls_ssl_context * a0, int a1), (a0, a1))
MBEDTLS_WRAP_VOID(mbedtls_ssl_set_hs_ca_chain, (mbedtls_ssl_context * a0, mbedtls_x509_crt * a1, mbedtls_x509_crl * a2), (a0, a1, a2))
MBEDTLS_WRAP_VOID(mbedtls_ssl_set_hs_dn_hints, (mbedtls_ssl_context * a0, const mbedtls_x509_crt * a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_set_hs_own_cert, (mbedtls_ssl_context * a0, mbedtls_x509_crt * a1, mbedtls_pk_context * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_set_hs_psk, (mbedtls_ssl_context * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_set_hs_psk_opaque, (mbedtls_ssl_context * a0, mbedtls_svc_key_id_t a1), (a0, a1), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_set_mtu, (mbedtls_ssl_context * a0, uint16_t a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_set_session, (mbedtls_ssl_context * a0, const mbedtls_ssl_session * a1), (a0, a1), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_set_timer_cb, (mbedtls_ssl_context * a0, void * a1, mbedtls_ssl_set_timer_t (*a2), mbedtls_ssl_get_timer_t (*a3)), (a0, a1, a2, a3))
MBEDTLS_WRAP_VOID(mbedtls_ssl_set_verify, (mbedtls_ssl_context * a0, int (*a1 ) (void *, mbedtls_x509_crt *, int, uint32_t *), void * a2), (a0, a1, a2))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_setup, (mbedtls_ssl_context * a0, const mbedtls_ssl_config * a1), (a0, a1), -1)
MBEDTLS_WRAP_VOID(mbedtls_ssl_ticket_free, (mbedtls_ssl_ticket_context * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_ssl_ticket_init, (mbedtls_ssl_ticket_context * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_ssl_ticket_rotate, (mbedtls_ssl_ticket_context * a0, const unsigned char * a1, size_t a2, const unsigned char * a3, size_t a4, uint32_t a5), (a0, a1, a2, a3, a4, a5), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_ticket_setup, (mbedtls_ssl_ticket_context * a0, psa_algorithm_t a1, psa_key_type_t a2, psa_key_bits_t a3, uint32_t a4), (a0, a1, a2, a3, a4), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_tls_prf, (const mbedtls_tls_prf_types a0, const unsigned char * a1, size_t a2, const char * a3, const unsigned char * a4, size_t a5, unsigned char * a6, size_t a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
MBEDTLS_WRAP_RET(int, mbedtls_ssl_write, (mbedtls_ssl_context * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_strerror, (int a0, char * a1, size_t a2), (a0, a1, a2))
MBEDTLS_WRAP_RET(int, mbedtls_timing_get_delay, (void * a0), (a0), -1)
MBEDTLS_WRAP_RET(uint32_t, mbedtls_timing_get_final_delay, (const mbedtls_timing_delay_context * a0), (a0), 0)
MBEDTLS_WRAP_RET(long long unsigned int, mbedtls_timing_get_timer, (struct mbedtls_timing_hr_time * a0, int a1), (a0, a1), ((long long unsigned int)0))
MBEDTLS_WRAP_VOID(mbedtls_timing_set_delay, (void * a0, uint32_t a1, uint32_t a2), (a0, a1, a2))
MBEDTLS_WRAP_RET(int, mbedtls_version_check_feature, (const char * a0), (a0), -1)
MBEDTLS_WRAP_RET(unsigned int, mbedtls_version_get_number, (), (), 0)
MBEDTLS_WRAP_RET(const char *, mbedtls_version_get_string, (), (), NULL)
MBEDTLS_WRAP_RET(const char *, mbedtls_version_get_string_full, (), (), NULL)
MBEDTLS_WRAP_VOID(mbedtls_x509_crl_free, (mbedtls_x509_crl * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_x509_crl_info, (char * a0, size_t a1, const char * a2, const mbedtls_x509_crl * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509_crl_init, (mbedtls_x509_crl * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_x509_crl_parse, (mbedtls_x509_crl * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crl_parse_der, (mbedtls_x509_crl * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crl_parse_file, (mbedtls_x509_crl * a0, const char * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_check_extended_key_usage, (const mbedtls_x509_crt * a0, const char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_check_key_usage, (const mbedtls_x509_crt * a0, unsigned int a1), (a0, a1), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509_crt_free, (mbedtls_x509_crt * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_get_ca_istrue, (const mbedtls_x509_crt * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_info, (char * a0, size_t a1, const char * a2, const mbedtls_x509_crt * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509_crt_init, (mbedtls_x509_crt * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_is_revoked, (const mbedtls_x509_crt * a0, const mbedtls_x509_crl * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_parse, (mbedtls_x509_crt * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(size_t, mbedtls_x509_crt_parse_cn_inet_pton, (const char * a0, void * a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_parse_der, (mbedtls_x509_crt * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_parse_der_nocopy, (mbedtls_x509_crt * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_parse_der_with_ext_cb, (mbedtls_x509_crt * a0, const unsigned char * a1, size_t a2, int a3, mbedtls_x509_crt_ext_cb_t a4, void * a5), (a0, a1, a2, a3, a4, a5), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_parse_file, (mbedtls_x509_crt * a0, const char * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_parse_path, (mbedtls_x509_crt * a0, const char * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_verify, (mbedtls_x509_crt * a0, mbedtls_x509_crt * a1, mbedtls_x509_crl * a2, const char * a3, uint32_t * a4, int (*a5 ) (void *, mbedtls_x509_crt *, int, uint32_t *), void * a6), (a0, a1, a2, a3, a4, a5, a6), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_verify_info, (char * a0, size_t a1, const char * a2, uint32_t a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_verify_restartable, (mbedtls_x509_crt * a0, mbedtls_x509_crt * a1, mbedtls_x509_crl * a2, const mbedtls_x509_crt_profile * a3, const char * a4, uint32_t * a5, int (*a6 ) (void *, mbedtls_x509_crt *, int, uint32_t *), void * a7, mbedtls_x509_crt_restart_ctx * a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_crt_verify_with_profile, (mbedtls_x509_crt * a0, mbedtls_x509_crt * a1, mbedtls_x509_crl * a2, const mbedtls_x509_crt_profile * a3, const char * a4, uint32_t * a5, int (*a6 ) (void *, mbedtls_x509_crt *, int, uint32_t *), void * a7), (a0, a1, a2, a3, a4, a5, a6, a7), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509_csr_free, (mbedtls_x509_csr * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_x509_csr_info, (char * a0, size_t a1, const char * a2, const mbedtls_x509_csr * a3), (a0, a1, a2, a3), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509_csr_init, (mbedtls_x509_csr * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_x509_csr_parse, (mbedtls_x509_csr * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_csr_parse_der, (mbedtls_x509_csr * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_csr_parse_der_with_ext_cb, (mbedtls_x509_csr * a0, const unsigned char * a1, size_t a2, mbedtls_x509_csr_ext_cb_t a3, void * a4), (a0, a1, a2, a3, a4), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_csr_parse_file, (mbedtls_x509_csr * a0, const char * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_dn_gets, (char * a0, size_t a1, const mbedtls_x509_name * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509_free_subject_alt_name, (mbedtls_x509_subject_alternative_name * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_x509_parse_subject_alt_name, (const mbedtls_x509_buf * a0, mbedtls_x509_subject_alternative_name * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(const char *, mbedtls_x509_pk_type_as_string, (const mbedtls_pk_context * a0), (a0), NULL)
MBEDTLS_WRAP_RET(int, mbedtls_x509_serial_gets, (char * a0, size_t a1, const mbedtls_x509_buf * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_string_to_names, (mbedtls_asn1_named_data ** a0, const char * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_time_cmp, (const mbedtls_x509_time * a0, const mbedtls_x509_time * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_time_gmtime, (mbedtls_time_t a0, mbedtls_x509_time * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_time_is_future, (const mbedtls_x509_time * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509_time_is_past, (const mbedtls_x509_time * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_der, (mbedtls_x509write_cert * a0, unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509write_crt_free, (mbedtls_x509write_cert * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_x509write_crt_init, (mbedtls_x509write_cert * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_pem, (mbedtls_x509write_cert * a0, unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_set_authority_key_identifier, (mbedtls_x509write_cert * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_set_basic_constraints, (mbedtls_x509write_cert * a0, int a1, int a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_set_ext_key_usage, (mbedtls_x509write_cert * a0, const mbedtls_asn1_sequence * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_set_extension, (mbedtls_x509write_cert * a0, const char * a1, size_t a2, int a3, const unsigned char * a4, size_t a5), (a0, a1, a2, a3, a4, a5), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509write_crt_set_issuer_key, (mbedtls_x509write_cert * a0, mbedtls_pk_context * a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_set_issuer_name, (mbedtls_x509write_cert * a0, const char * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_set_key_usage, (mbedtls_x509write_cert * a0, unsigned int a1), (a0, a1), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509write_crt_set_md_alg, (mbedtls_x509write_cert * a0, mbedtls_md_type_t a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_set_ns_cert_type, (mbedtls_x509write_cert * a0, unsigned char a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_set_serial_raw, (mbedtls_x509write_cert * a0, const unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_set_subject_alternative_name, (mbedtls_x509write_cert * a0, const mbedtls_x509_san_list * a1), (a0, a1), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509write_crt_set_subject_key, (mbedtls_x509write_cert * a0, mbedtls_pk_context * a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_set_subject_key_identifier, (mbedtls_x509write_cert * a0), (a0), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_set_subject_name, (mbedtls_x509write_cert * a0, const char * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_crt_set_validity, (mbedtls_x509write_cert * a0, const char * a1, const char * a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509write_crt_set_version, (mbedtls_x509write_cert * a0, int a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_x509write_csr_der, (mbedtls_x509write_csr * a0, unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509write_csr_free, (mbedtls_x509write_csr * a0), (a0))
MBEDTLS_WRAP_VOID(mbedtls_x509write_csr_init, (mbedtls_x509write_csr * a0), (a0))
MBEDTLS_WRAP_RET(int, mbedtls_x509write_csr_pem, (mbedtls_x509write_csr * a0, unsigned char * a1, size_t a2), (a0, a1, a2), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_csr_set_extension, (mbedtls_x509write_csr * a0, const char * a1, size_t a2, int a3, const unsigned char * a4, size_t a5), (a0, a1, a2, a3, a4, a5), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509write_csr_set_key, (mbedtls_x509write_csr * a0, mbedtls_pk_context * a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_x509write_csr_set_key_usage, (mbedtls_x509write_csr * a0, unsigned char a1), (a0, a1), -1)
MBEDTLS_WRAP_VOID(mbedtls_x509write_csr_set_md_alg, (mbedtls_x509write_csr * a0, mbedtls_md_type_t a1), (a0, a1))
MBEDTLS_WRAP_RET(int, mbedtls_x509write_csr_set_ns_cert_type, (mbedtls_x509write_csr * a0, unsigned char a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_csr_set_subject_alternative_name, (mbedtls_x509write_csr * a0, const mbedtls_x509_san_list * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(int, mbedtls_x509write_csr_set_subject_name, (mbedtls_x509write_csr * a0, const char * a1), (a0, a1), -1)
MBEDTLS_WRAP_RET(psa_status_t, psa_aead_abort, (psa_aead_operation_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_aead_decrypt, (mbedtls_svc_key_id_t a0, psa_algorithm_t a1, const uint8_t * a2, size_t a3, const uint8_t * a4, size_t a5, const uint8_t * a6, size_t a7, uint8_t * a8, size_t a9, size_t * a10), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_aead_decrypt_setup, (psa_aead_operation_t * a0, mbedtls_svc_key_id_t a1, psa_algorithm_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_aead_encrypt, (mbedtls_svc_key_id_t a0, psa_algorithm_t a1, const uint8_t * a2, size_t a3, const uint8_t * a4, size_t a5, const uint8_t * a6, size_t a7, uint8_t * a8, size_t a9, size_t * a10), (a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_aead_encrypt_setup, (psa_aead_operation_t * a0, mbedtls_svc_key_id_t a1, psa_algorithm_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_aead_finish, (psa_aead_operation_t * a0, uint8_t * a1, size_t a2, size_t * a3, uint8_t * a4, size_t a5, size_t * a6), (a0, a1, a2, a3, a4, a5, a6), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_aead_generate_nonce, (psa_aead_operation_t * a0, uint8_t * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_aead_set_lengths, (psa_aead_operation_t * a0, size_t a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_aead_set_nonce, (psa_aead_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_aead_update, (psa_aead_operation_t * a0, const uint8_t * a1, size_t a2, uint8_t * a3, size_t a4, size_t * a5), (a0, a1, a2, a3, a4, a5), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_aead_update_ad, (psa_aead_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_aead_verify, (psa_aead_operation_t * a0, uint8_t * a1, size_t a2, size_t * a3, const uint8_t * a4, size_t a5), (a0, a1, a2, a3, a4, a5), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_asymmetric_decrypt, (mbedtls_svc_key_id_t a0, psa_algorithm_t a1, const uint8_t * a2, size_t a3, const uint8_t * a4, size_t a5, uint8_t * a6, size_t a7, size_t * a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_asymmetric_encrypt, (mbedtls_svc_key_id_t a0, psa_algorithm_t a1, const uint8_t * a2, size_t a3, const uint8_t * a4, size_t a5, uint8_t * a6, size_t a7, size_t * a8), (a0, a1, a2, a3, a4, a5, a6, a7, a8), 0)
MBEDTLS_WRAP_RET(int, psa_can_do_hash, (psa_algorithm_t a0), (a0), -1)
MBEDTLS_WRAP_RET(psa_status_t, psa_cipher_abort, (psa_cipher_operation_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_cipher_decrypt, (mbedtls_svc_key_id_t a0, psa_algorithm_t a1, const uint8_t * a2, size_t a3, uint8_t * a4, size_t a5, size_t * a6), (a0, a1, a2, a3, a4, a5, a6), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_cipher_decrypt_setup, (psa_cipher_operation_t * a0, mbedtls_svc_key_id_t a1, psa_algorithm_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_cipher_encrypt, (mbedtls_svc_key_id_t a0, psa_algorithm_t a1, const uint8_t * a2, size_t a3, uint8_t * a4, size_t a5, size_t * a6), (a0, a1, a2, a3, a4, a5, a6), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_cipher_encrypt_setup, (psa_cipher_operation_t * a0, mbedtls_svc_key_id_t a1, psa_algorithm_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_cipher_finish, (psa_cipher_operation_t * a0, uint8_t * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_cipher_generate_iv, (psa_cipher_operation_t * a0, uint8_t * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_cipher_set_iv, (psa_cipher_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_cipher_update, (psa_cipher_operation_t * a0, const uint8_t * a1, size_t a2, uint8_t * a3, size_t a4, size_t * a5), (a0, a1, a2, a3, a4, a5), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_copy_key, (mbedtls_svc_key_id_t a0, const psa_key_attributes_t * a1, mbedtls_svc_key_id_t * a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_crypto_driver_pake_get_cipher_suite, (const psa_crypto_driver_pake_inputs_t * a0, psa_pake_cipher_suite_t * a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_crypto_driver_pake_get_password, (const psa_crypto_driver_pake_inputs_t * a0, uint8_t * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_crypto_driver_pake_get_password_len, (const psa_crypto_driver_pake_inputs_t * a0, size_t * a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_crypto_driver_pake_get_peer, (const psa_crypto_driver_pake_inputs_t * a0, uint8_t * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_crypto_driver_pake_get_peer_len, (const psa_crypto_driver_pake_inputs_t * a0, size_t * a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_crypto_driver_pake_get_user, (const psa_crypto_driver_pake_inputs_t * a0, uint8_t * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_crypto_driver_pake_get_user_len, (const psa_crypto_driver_pake_inputs_t * a0, size_t * a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_crypto_init, (), (), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_destroy_key, (mbedtls_svc_key_id_t a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_export_key, (mbedtls_svc_key_id_t a0, uint8_t * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_export_public_key, (mbedtls_svc_key_id_t a0, uint8_t * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_export_public_key_iop_abort, (psa_export_public_key_iop_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_export_public_key_iop_complete, (psa_export_public_key_iop_t * a0, uint8_t * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(uint32_t, psa_export_public_key_iop_get_num_ops, (psa_export_public_key_iop_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_export_public_key_iop_setup, (psa_export_public_key_iop_t * a0, mbedtls_svc_key_id_t a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_generate_key, (const psa_key_attributes_t * a0, mbedtls_svc_key_id_t * a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_generate_key_custom, (const psa_key_attributes_t * a0, const psa_custom_key_parameters_t * a1, const uint8_t * a2, size_t a3, mbedtls_svc_key_id_t * a4), (a0, a1, a2, a3, a4), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_generate_key_iop_abort, (psa_generate_key_iop_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_generate_key_iop_complete, (psa_generate_key_iop_t * a0, mbedtls_svc_key_id_t * a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(uint32_t, psa_generate_key_iop_get_num_ops, (psa_generate_key_iop_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_generate_key_iop_setup, (psa_generate_key_iop_t * a0, const psa_key_attributes_t * a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_generate_random, (uint8_t * a0, size_t a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_get_key_attributes, (mbedtls_svc_key_id_t a0, psa_key_attributes_t * a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_hash_abort, (psa_hash_operation_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_hash_clone, (const psa_hash_operation_t * a0, psa_hash_operation_t * a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_hash_compare, (psa_algorithm_t a0, const uint8_t * a1, size_t a2, const uint8_t * a3, size_t a4), (a0, a1, a2, a3, a4), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_hash_compute, (psa_algorithm_t a0, const uint8_t * a1, size_t a2, uint8_t * a3, size_t a4, size_t * a5), (a0, a1, a2, a3, a4, a5), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_hash_finish, (psa_hash_operation_t * a0, uint8_t * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_hash_setup, (psa_hash_operation_t * a0, psa_algorithm_t a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_hash_update, (psa_hash_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_hash_verify, (psa_hash_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_import_key, (const psa_key_attributes_t * a0, const uint8_t * a1, size_t a2, mbedtls_svc_key_id_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(uint32_t, psa_interruptible_get_max_ops, (), (), 0)
MBEDTLS_WRAP_VOID(psa_interruptible_set_max_ops, (uint32_t a0), (a0))
MBEDTLS_WRAP_RET(psa_status_t, psa_key_agreement, (mbedtls_svc_key_id_t a0, const uint8_t * a1, size_t a2, psa_algorithm_t a3, const psa_key_attributes_t * a4, mbedtls_svc_key_id_t * a5), (a0, a1, a2, a3, a4, a5), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_agreement_iop_abort, (psa_key_agreement_iop_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_agreement_iop_complete, (psa_key_agreement_iop_t * a0, mbedtls_svc_key_id_t * a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(uint32_t, psa_key_agreement_iop_get_num_ops, (psa_key_agreement_iop_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_agreement_iop_setup, (psa_key_agreement_iop_t * a0, mbedtls_svc_key_id_t a1, const uint8_t * a2, size_t a3, psa_algorithm_t a4, const psa_key_attributes_t * a5), (a0, a1, a2, a3, a4, a5), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_abort, (psa_key_derivation_operation_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_get_capacity, (const psa_key_derivation_operation_t * a0, size_t * a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_input_bytes, (psa_key_derivation_operation_t * a0, psa_key_derivation_step_t a1, const uint8_t * a2, size_t a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_input_integer, (psa_key_derivation_operation_t * a0, psa_key_derivation_step_t a1, uint64_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_input_key, (psa_key_derivation_operation_t * a0, psa_key_derivation_step_t a1, mbedtls_svc_key_id_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_key_agreement, (psa_key_derivation_operation_t * a0, psa_key_derivation_step_t a1, mbedtls_svc_key_id_t a2, const uint8_t * a3, size_t a4), (a0, a1, a2, a3, a4), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_output_bytes, (psa_key_derivation_operation_t * a0, uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_output_key, (const psa_key_attributes_t * a0, psa_key_derivation_operation_t * a1, mbedtls_svc_key_id_t * a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_output_key_custom, (const psa_key_attributes_t * a0, psa_key_derivation_operation_t * a1, const psa_custom_key_parameters_t * a2, const uint8_t * a3, size_t a4, mbedtls_svc_key_id_t * a5), (a0, a1, a2, a3, a4, a5), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_set_capacity, (psa_key_derivation_operation_t * a0, size_t a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_setup, (psa_key_derivation_operation_t * a0, psa_algorithm_t a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_verify_bytes, (psa_key_derivation_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_key_derivation_verify_key, (psa_key_derivation_operation_t * a0, psa_key_id_t a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_mac_abort, (psa_mac_operation_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_mac_compute, (mbedtls_svc_key_id_t a0, psa_algorithm_t a1, const uint8_t * a2, size_t a3, uint8_t * a4, size_t a5, size_t * a6), (a0, a1, a2, a3, a4, a5, a6), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_mac_sign_finish, (psa_mac_operation_t * a0, uint8_t * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_mac_sign_setup, (psa_mac_operation_t * a0, mbedtls_svc_key_id_t a1, psa_algorithm_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_mac_update, (psa_mac_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_mac_verify, (mbedtls_svc_key_id_t a0, psa_algorithm_t a1, const uint8_t * a2, size_t a3, const uint8_t * a4, size_t a5), (a0, a1, a2, a3, a4, a5), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_mac_verify_finish, (psa_mac_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_mac_verify_setup, (psa_mac_operation_t * a0, mbedtls_svc_key_id_t a1, psa_algorithm_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_pake_abort, (psa_pake_operation_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_pake_get_shared_key, (psa_pake_operation_t * a0, const psa_key_attributes_t * a1, mbedtls_svc_key_id_t * a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_pake_input, (psa_pake_operation_t * a0, psa_pake_step_t a1, const uint8_t * a2, size_t a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_pake_output, (psa_pake_operation_t * a0, psa_pake_step_t a1, uint8_t * a2, size_t a3, size_t * a4), (a0, a1, a2, a3, a4), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_pake_set_context, (psa_pake_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_pake_set_peer, (psa_pake_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_pake_set_role, (psa_pake_operation_t * a0, psa_pake_role_t a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_pake_set_user, (psa_pake_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_pake_setup, (psa_pake_operation_t * a0, mbedtls_svc_key_id_t a1, const psa_pake_cipher_suite_t * a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_purge_key, (mbedtls_svc_key_id_t a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_random_deplete, (), (), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_random_reseed, (const uint8_t * a0, size_t a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_random_set_prediction_resistance, (unsigned int a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_raw_key_agreement, (psa_algorithm_t a0, mbedtls_svc_key_id_t a1, const uint8_t * a2, size_t a3, uint8_t * a4, size_t a5, size_t * a6), (a0, a1, a2, a3, a4, a5, a6), 0)
MBEDTLS_WRAP_VOID(psa_reset_key_attributes, (psa_key_attributes_t * a0), (a0))
MBEDTLS_WRAP_RET(psa_status_t, psa_sign_hash, (mbedtls_svc_key_id_t a0, psa_algorithm_t a1, const uint8_t * a2, size_t a3, uint8_t * a4, size_t a5, size_t * a6), (a0, a1, a2, a3, a4, a5, a6), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_sign_hash_abort, (psa_sign_hash_interruptible_operation_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_sign_hash_complete, (psa_sign_hash_interruptible_operation_t * a0, uint8_t * a1, size_t a2, size_t * a3), (a0, a1, a2, a3), 0)
MBEDTLS_WRAP_RET(uint32_t, psa_sign_hash_get_num_ops, (const psa_sign_hash_interruptible_operation_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_sign_hash_start, (psa_sign_hash_interruptible_operation_t * a0, mbedtls_svc_key_id_t a1, psa_algorithm_t a2, const uint8_t * a3, size_t a4), (a0, a1, a2, a3, a4), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_sign_message, (mbedtls_svc_key_id_t a0, psa_algorithm_t a1, const uint8_t * a2, size_t a3, uint8_t * a4, size_t a5, size_t * a6), (a0, a1, a2, a3, a4, a5, a6), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_verify_hash, (mbedtls_svc_key_id_t a0, psa_algorithm_t a1, const uint8_t * a2, size_t a3, const uint8_t * a4, size_t a5), (a0, a1, a2, a3, a4, a5), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_verify_hash_abort, (psa_verify_hash_interruptible_operation_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_verify_hash_complete, (psa_verify_hash_interruptible_operation_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(uint32_t, psa_verify_hash_get_num_ops, (const psa_verify_hash_interruptible_operation_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_verify_hash_start, (psa_verify_hash_interruptible_operation_t * a0, mbedtls_svc_key_id_t a1, psa_algorithm_t a2, const uint8_t * a3, size_t a4, const uint8_t * a5, size_t a6), (a0, a1, a2, a3, a4, a5, a6), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_verify_message, (mbedtls_svc_key_id_t a0, psa_algorithm_t a1, const uint8_t * a2, size_t a3, const uint8_t * a4, size_t a5), (a0, a1, a2, a3, a4, a5), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_xof_abort, (psa_xof_operation_t * a0), (a0), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_xof_output, (psa_xof_operation_t * a0, uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_xof_set_context, (psa_xof_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_xof_setup, (psa_xof_operation_t * a0, psa_algorithm_t a1), (a0, a1), 0)
MBEDTLS_WRAP_RET(psa_status_t, psa_xof_update, (psa_xof_operation_t * a0, const uint8_t * a1, size_t a2), (a0, a1, a2), 0)
/* ---------- 未封装（头文件未声明或不可 dlsym，共 764 个） ---------- */
/* mbedtls_aes_cmac_prf_128 (头未声明) */
/* mbedtls_aes_crypt_cbc (头未声明) */
/* mbedtls_aes_crypt_cfb128 (头未声明) */
/* mbedtls_aes_crypt_cfb8 (头未声明) */
/* mbedtls_aes_crypt_ctr (头未声明) */
/* mbedtls_aes_crypt_ecb (头未声明) */
/* mbedtls_aes_crypt_ofb (头未声明) */
/* mbedtls_aes_free (头未声明) */
/* mbedtls_aes_get_implementation (头未声明) */
/* mbedtls_aes_init (头未声明) */
/* mbedtls_aes_self_test (头未声明) */
/* mbedtls_aes_setkey_dec (头未声明) */
/* mbedtls_aes_setkey_enc (头未声明) */
/* mbedtls_aesni_crypt_ecb (头未声明) */
/* mbedtls_aesni_gcm_mult (头未声明) */
/* mbedtls_aesni_has_support (头未声明) */
/* mbedtls_aesni_inverse_key (头未声明) */
/* mbedtls_aesni_setkey_enc (头未声明) */
/* mbedtls_aria_crypt_cbc (头未声明) */
/* mbedtls_aria_crypt_cfb128 (头未声明) */
/* mbedtls_aria_crypt_ctr (头未声明) */
/* mbedtls_aria_crypt_ecb (头未声明) */
/* mbedtls_aria_free (头未声明) */
/* mbedtls_aria_init (头未声明) */
/* mbedtls_aria_self_test (头未声明) */
/* mbedtls_aria_setkey_dec (头未声明) */
/* mbedtls_aria_setkey_enc (头未声明) */
/* mbedtls_asn1_get_mpi (头未声明) */
/* mbedtls_camellia_crypt_cbc (头未声明) */
/* mbedtls_camellia_crypt_cfb128 (头未声明) */
/* mbedtls_camellia_crypt_ctr (头未声明) */
/* mbedtls_camellia_crypt_ecb (头未声明) */
/* mbedtls_camellia_free (头未声明) */
/* mbedtls_camellia_init (头未声明) */
/* mbedtls_camellia_self_test (头未声明) */
/* mbedtls_camellia_setkey_dec (头未声明) */
/* mbedtls_camellia_setkey_enc (头未声明) */
/* mbedtls_ccm_auth_decrypt (头未声明) */
/* mbedtls_ccm_encrypt_and_tag (头未声明) */
/* mbedtls_ccm_finish (头未声明) */
/* mbedtls_ccm_free (头未声明) */
/* mbedtls_ccm_init (头未声明) */
/* mbedtls_ccm_self_test (头未声明) */
/* mbedtls_ccm_set_lengths (头未声明) */
/* mbedtls_ccm_setkey (头未声明) */
/* mbedtls_ccm_star_auth_decrypt (头未声明) */
/* mbedtls_ccm_star_encrypt_and_tag (头未声明) */
/* mbedtls_ccm_starts (头未声明) */
/* mbedtls_ccm_update (头未声明) */
/* mbedtls_ccm_update_ad (头未声明) */
/* mbedtls_chacha20_check_counter_wrap (头未声明) */
/* mbedtls_chacha20_crypt (头未声明) */
/* mbedtls_chacha20_free (头未声明) */
/* mbedtls_chacha20_init (头未声明) */
/* mbedtls_chacha20_self_test (头未声明) */
/* mbedtls_chacha20_setkey (头未声明) */
/* mbedtls_chacha20_starts (头未声明) */
/* mbedtls_chacha20_update (头未声明) */
/* mbedtls_chachapoly_auth_decrypt (头未声明) */
/* mbedtls_chachapoly_encrypt_and_tag (头未声明) */
/* mbedtls_chachapoly_finish (头未声明) */
/* mbedtls_chachapoly_free (头未声明) */
/* mbedtls_chachapoly_init (头未声明) */
/* mbedtls_chachapoly_self_test (头未声明) */
/* mbedtls_chachapoly_setkey (头未声明) */
/* mbedtls_chachapoly_starts (头未声明) */
/* mbedtls_chachapoly_update (头未声明) */
/* mbedtls_chachapoly_update_aad (头未声明) */
/* mbedtls_cipher_auth_decrypt_ext (头未声明) */
/* mbedtls_cipher_auth_encrypt_ext (头未声明) */
/* mbedtls_cipher_check_tag (头未声明) */
/* mbedtls_cipher_cmac (头未声明) */
/* mbedtls_cipher_cmac_finish (头未声明) */
/* mbedtls_cipher_cmac_reset (头未声明) */
/* mbedtls_cipher_cmac_starts (头未声明) */
/* mbedtls_cipher_cmac_update (头未声明) */
/* mbedtls_cipher_crypt (头未声明) */
/* mbedtls_cipher_finish (头未声明) */
/* mbedtls_cipher_finish_padded (头未声明) */
/* mbedtls_cipher_free (头未声明) */
/* mbedtls_cipher_info_from_psa (头未声明) */
/* mbedtls_cipher_info_from_string (头未声明) */
/* mbedtls_cipher_info_from_type (头未声明) */
/* mbedtls_cipher_info_from_values (头未声明) */
/* mbedtls_cipher_init (头未声明) */
/* mbedtls_cipher_list (头未声明) */
/* mbedtls_cipher_reset (头未声明) */
/* mbedtls_cipher_set_iv (头未声明) */
/* mbedtls_cipher_set_padding_mode (头未声明) */
/* mbedtls_cipher_setkey (头未声明) */
/* mbedtls_cipher_setup (头未声明) */
/* mbedtls_cipher_update (头未声明) */
/* mbedtls_cipher_update_ad (头未声明) */
/* mbedtls_cipher_values_from_psa (头未声明) */
/* mbedtls_cipher_write_tag (头未声明) */
/* mbedtls_cmac_self_test (头未声明) */
/* mbedtls_ct_memcmp_partial (头未声明) */
/* mbedtls_ct_memcpy_if (头未声明) */
/* mbedtls_ct_memcpy_offset (头未声明) */
/* mbedtls_ct_memmove_left (头未声明) */
/* mbedtls_ct_zeroize_if (头未声明) */
/* mbedtls_ctr_drbg_free (头未声明) */
/* mbedtls_ctr_drbg_init (头未声明) */
/* mbedtls_ctr_drbg_random (头未声明) */
/* mbedtls_ctr_drbg_random_with_add (头未声明) */
/* mbedtls_ctr_drbg_reseed (头未声明) */
/* mbedtls_ctr_drbg_seed (头未声明) */
/* mbedtls_ctr_drbg_self_test (头未声明) */
/* mbedtls_ctr_drbg_set_entropy_len (头未声明) */
/* mbedtls_ctr_drbg_set_nonce_len (头未声明) */
/* mbedtls_ctr_drbg_set_prediction_resistance (头未声明) */
/* mbedtls_ctr_drbg_set_reseed_interval (头未声明) */
/* mbedtls_ctr_drbg_update (头未声明) */
/* mbedtls_ctr_drbg_update_seed_file (头未声明) */
/* mbedtls_ctr_drbg_write_seed_file (头未声明) */
/* mbedtls_debug_print_buf (头未声明) */
/* mbedtls_debug_print_crt (头未声明) */
/* mbedtls_debug_print_msg (头未声明) */
/* mbedtls_debug_print_ret (头未声明) */
/* mbedtls_debug_snprintf (头未声明) */
/* mbedtls_ecc_group_from_psa (头未声明) */
/* mbedtls_ecc_group_to_psa (头未声明) */
/* mbedtls_ecdsa_can_do (头未声明) */
/* mbedtls_ecdsa_free (头未声明) */
/* mbedtls_ecdsa_from_keypair (头未声明) */
/* mbedtls_ecdsa_genkey (头未声明) */
/* mbedtls_ecdsa_init (头未声明) */
/* mbedtls_ecdsa_read_signature (头未声明) */
/* mbedtls_ecdsa_read_signature_restartable (头未声明) */
/* mbedtls_ecdsa_sign (头未声明) */
/* mbedtls_ecdsa_sign_det_ext (头未声明) */
/* mbedtls_ecdsa_sign_det_restartable (头未声明) */
/* mbedtls_ecdsa_sign_restartable (头未声明) */
/* mbedtls_ecdsa_verify (头未声明) */
/* mbedtls_ecdsa_verify_restartable (头未声明) */
/* mbedtls_ecdsa_write_signature (头未声明) */
/* mbedtls_ecdsa_write_signature_restartable (头未声明) */
/* mbedtls_ecjpake_check (头未声明) */
/* mbedtls_ecjpake_derive_secret (头未声明) */
/* mbedtls_ecjpake_free (头未声明) */
/* mbedtls_ecjpake_init (头未声明) */
/* mbedtls_ecjpake_read_round_one (头未声明) */
/* mbedtls_ecjpake_read_round_two (头未声明) */
/* mbedtls_ecjpake_self_test (头未声明) */
/* mbedtls_ecjpake_set_point_format (头未声明) */
/* mbedtls_ecjpake_setup (头未声明) */
/* mbedtls_ecjpake_write_round_one (头未声明) */
/* mbedtls_ecjpake_write_round_two (头未声明) */
/* mbedtls_ecjpake_write_shared_key (头未声明) */
/* mbedtls_ecp_check_privkey (头未声明) */
/* mbedtls_ecp_check_pub_priv (头未声明) */
/* mbedtls_ecp_check_pubkey (头未声明) */
/* mbedtls_ecp_copy (头未声明) */
/* mbedtls_ecp_curve_info_from_grp_id (头未声明) */
/* mbedtls_ecp_curve_info_from_name (头未声明) */
/* mbedtls_ecp_curve_info_from_tls_id (头未声明) */
/* mbedtls_ecp_curve_list (头未声明) */
/* mbedtls_ecp_export (头未声明) */
/* mbedtls_ecp_gen_key (头未声明) */
/* mbedtls_ecp_gen_keypair (头未声明) */
/* mbedtls_ecp_gen_keypair_base (头未声明) */
/* mbedtls_ecp_gen_privkey (头未声明) */
/* mbedtls_ecp_get_type (头未声明) */
/* mbedtls_ecp_group_copy (头未声明) */
/* mbedtls_ecp_group_free (头未声明) */
/* mbedtls_ecp_group_init (头未声明) */
/* mbedtls_ecp_group_load (头未声明) */
/* mbedtls_ecp_grp_id_list (头未声明) */
/* mbedtls_ecp_is_zero (头未声明) */
/* mbedtls_ecp_keypair_calc_public (头未声明) */
/* mbedtls_ecp_keypair_free (头未声明) */
/* mbedtls_ecp_keypair_get_group_id (头未声明) */
/* mbedtls_ecp_keypair_init (头未声明) */
/* mbedtls_ecp_mul (头未声明) */
/* mbedtls_ecp_mul_restartable (头未声明) */
/* mbedtls_ecp_muladd (头未声明) */
/* mbedtls_ecp_muladd_restartable (头未声明) */
/* mbedtls_ecp_point_cmp (头未声明) */
/* mbedtls_ecp_point_free (头未声明) */
/* mbedtls_ecp_point_init (头未声明) */
/* mbedtls_ecp_point_read_binary (头未声明) */
/* mbedtls_ecp_point_read_string (头未声明) */
/* mbedtls_ecp_point_write_binary (头未声明) */
/* mbedtls_ecp_read_key (头未声明) */
/* mbedtls_ecp_self_test (头未声明) */
/* mbedtls_ecp_set_public_key (头未声明) */
/* mbedtls_ecp_set_zero (头未声明) */
/* mbedtls_ecp_tls_read_group (头未声明) */
/* mbedtls_ecp_tls_read_group_id (头未声明) */
/* mbedtls_ecp_tls_read_point (头未声明) */
/* mbedtls_ecp_tls_write_group (头未声明) */
/* mbedtls_ecp_tls_write_point (头未声明) */
/* mbedtls_ecp_write_key_ext (头未声明) */
/* mbedtls_ecp_write_public_key (头未声明) */
/* mbedtls_entropy_add_source (头未声明) */
/* mbedtls_entropy_free (头未声明) */
/* mbedtls_entropy_func (头未声明) */
/* mbedtls_entropy_gather (头未声明) */
/* mbedtls_entropy_init (头未声明) */
/* mbedtls_entropy_poll_platform (头未声明) */
/* mbedtls_entropy_self_test (头未声明) */
/* mbedtls_entropy_update_manual (头未声明) */
/* mbedtls_entropy_update_seed_file (头未声明) */
/* mbedtls_entropy_write_seed_file (头未声明) */
/* mbedtls_gcm_auth_decrypt (头未声明) */
/* mbedtls_gcm_crypt_and_tag (头未声明) */
/* mbedtls_gcm_finish (头未声明) */
/* mbedtls_gcm_free (头未声明) */
/* mbedtls_gcm_init (头未声明) */
/* mbedtls_gcm_self_test (头未声明) */
/* mbedtls_gcm_setkey (头未声明) */
/* mbedtls_gcm_starts (头未声明) */
/* mbedtls_gcm_update (头未声明) */
/* mbedtls_gcm_update_ad (头未声明) */
/* mbedtls_hmac_drbg_free (头未声明) */
/* mbedtls_hmac_drbg_init (头未声明) */
/* mbedtls_hmac_drbg_random (头未声明) */
/* mbedtls_hmac_drbg_random_with_add (头未声明) */
/* mbedtls_hmac_drbg_reseed (头未声明) */
/* mbedtls_hmac_drbg_seed (头未声明) */
/* mbedtls_hmac_drbg_seed_buf (头未声明) */
/* mbedtls_hmac_drbg_self_test (头未声明) */
/* mbedtls_hmac_drbg_set_entropy_len (头未声明) */
/* mbedtls_hmac_drbg_set_prediction_resistance (头未声明) */
/* mbedtls_hmac_drbg_set_reseed_interval (头未声明) */
/* mbedtls_hmac_drbg_update (头未声明) */
/* mbedtls_hmac_drbg_update_seed_file (头未声明) */
/* mbedtls_hmac_drbg_write_seed_file (头未声明) */
/* mbedtls_lmots_calculate_public_key_candidate (头未声明) */
/* mbedtls_lmots_export_public_key (头未声明) */
/* mbedtls_lmots_import_public_key (头未声明) */
/* mbedtls_lmots_public_free (头未声明) */
/* mbedtls_lmots_public_init (头未声明) */
/* mbedtls_lmots_verify (头未声明) */
/* mbedtls_md5 (头未声明) */
/* mbedtls_md5_clone (头未声明) */
/* mbedtls_md5_finish (头未声明) */
/* mbedtls_md5_free (头未声明) */
/* mbedtls_md5_init (头未声明) */
/* mbedtls_md5_self_test (头未声明) */
/* mbedtls_md5_starts (头未声明) */
/* mbedtls_md5_update (头未声明) */
/* mbedtls_md_error_from_psa (头未声明) */
/* mbedtls_md_file (头未声明) */
/* mbedtls_md_get_name (头未声明) */
/* mbedtls_md_hmac (头未声明) */
/* mbedtls_md_hmac_finish (头未声明) */
/* mbedtls_md_hmac_reset (头未声明) */
/* mbedtls_md_hmac_setup (头未声明) */
/* mbedtls_md_hmac_starts (头未声明) */
/* mbedtls_md_hmac_update (头未声明) */
/* mbedtls_md_info_from_ctx (头未声明) */
/* mbedtls_md_info_from_string (头未声明) */
/* mbedtls_md_list (头未声明) */
/* mbedtls_mpi_add_abs (头未声明) */
/* mbedtls_mpi_add_int (头未声明) */
/* mbedtls_mpi_add_mpi (头未声明) */
/* mbedtls_mpi_bitlen (头未声明) */
/* mbedtls_mpi_cmp_abs (头未声明) */
/* mbedtls_mpi_cmp_int (头未声明) */
/* mbedtls_mpi_cmp_mpi (头未声明) */
/* mbedtls_mpi_copy (头未声明) */
/* mbedtls_mpi_core_add (头未声明) */
/* mbedtls_mpi_core_add_if (头未声明) */
/* mbedtls_mpi_core_bigendian_to_host (头未声明) */
/* mbedtls_mpi_core_bitlen (头未声明) */
/* mbedtls_mpi_core_check_zero_ct (头未声明) */
/* mbedtls_mpi_core_clz (头未声明) */
/* mbedtls_mpi_core_cond_assign (头未声明) */
/* mbedtls_mpi_core_cond_swap (头未声明) */
/* mbedtls_mpi_core_exp_mod (头未声明) */
/* mbedtls_mpi_core_exp_mod_unsafe (头未声明) */
/* mbedtls_mpi_core_exp_mod_working_limbs (头未声明) */
/* mbedtls_mpi_core_fill_random (头未声明) */
/* mbedtls_mpi_core_from_mont_rep (头未声明) */
/* mbedtls_mpi_core_gcd_modinv_odd (头未声明) */
/* mbedtls_mpi_core_get_mont_r2_unsafe (头未声明) */
/* mbedtls_mpi_core_lt_ct (头未声明) */
/* mbedtls_mpi_core_mla (头未声明) */
/* mbedtls_mpi_core_montmul (头未声明) */
/* mbedtls_mpi_core_montmul_init (头未声明) */
/* mbedtls_mpi_core_mul (头未声明) */
/* mbedtls_mpi_core_random (头未声明) */
/* mbedtls_mpi_core_read_be (头未声明) */
/* mbedtls_mpi_core_read_le (头未声明) */
/* mbedtls_mpi_core_shift_l (头未声明) */
/* mbedtls_mpi_core_shift_r (头未声明) */
/* mbedtls_mpi_core_sub (头未声明) */
/* mbedtls_mpi_core_sub_int (头未声明) */
/* mbedtls_mpi_core_to_mont_rep (头未声明) */
/* mbedtls_mpi_core_uint_le_mpi (头未声明) */
/* mbedtls_mpi_core_write_be (头未声明) */
/* mbedtls_mpi_core_write_le (头未声明) */
/* mbedtls_mpi_div_int (头未声明) */
/* mbedtls_mpi_div_mpi (头未声明) */
/* mbedtls_mpi_exp_mod (头未声明) */
/* mbedtls_mpi_exp_mod_unsafe (头未声明) */
/* mbedtls_mpi_fill_random (头未声明) */
/* mbedtls_mpi_free (头未声明) */
/* mbedtls_mpi_gcd (头未声明) */
/* mbedtls_mpi_gcd_modinv_odd (头未声明) */
/* mbedtls_mpi_gen_prime (头未声明) */
/* mbedtls_mpi_get_bit (头未声明) */
/* mbedtls_mpi_grow (头未声明) */
/* mbedtls_mpi_init (头未声明) */
/* mbedtls_mpi_inv_mod (头未声明) */
/* mbedtls_mpi_inv_mod_even_in_range (头未声明) */
/* mbedtls_mpi_inv_mod_odd (头未声明) */
/* mbedtls_mpi_is_prime_ext (头未声明) */
/* mbedtls_mpi_lsb (头未声明) */
/* mbedtls_mpi_lset (头未声明) */
/* mbedtls_mpi_lt_mpi_ct (头未声明) */
/* mbedtls_mpi_mod_int (头未声明) */
/* mbedtls_mpi_mod_mpi (头未声明) */
/* mbedtls_mpi_mul_int (头未声明) */
/* mbedtls_mpi_mul_mpi (头未声明) */
/* mbedtls_mpi_random (头未声明) */
/* mbedtls_mpi_read_binary (头未声明) */
/* mbedtls_mpi_read_binary_le (头未声明) */
/* mbedtls_mpi_read_file (头未声明) */
/* mbedtls_mpi_read_string (头未声明) */
/* mbedtls_mpi_safe_cond_assign (头未声明) */
/* mbedtls_mpi_safe_cond_swap (头未声明) */
/* mbedtls_mpi_self_test (头未声明) */
/* mbedtls_mpi_set_bit (头未声明) */
/* mbedtls_mpi_shift_l (头未声明) */
/* mbedtls_mpi_shift_r (头未声明) */
/* mbedtls_mpi_shrink (头未声明) */
/* mbedtls_mpi_size (头未声明) */
/* mbedtls_mpi_sub_abs (头未声明) */
/* mbedtls_mpi_sub_int (头未声明) */
/* mbedtls_mpi_sub_mpi (头未声明) */
/* mbedtls_mpi_swap (头未声明) */
/* mbedtls_mpi_write_binary (头未声明) */
/* mbedtls_mpi_write_binary_le (头未声明) */
/* mbedtls_mpi_write_file (头未声明) */
/* mbedtls_mpi_write_string (头未声明) */
/* mbedtls_mps_reader_commit (头未声明) */
/* mbedtls_mps_reader_feed (头未声明) */
/* mbedtls_mps_reader_free (头未声明) */
/* mbedtls_mps_reader_get (头未声明) */
/* mbedtls_mps_reader_init (头未声明) */
/* mbedtls_mps_reader_reclaim (头未声明) */
/* mbedtls_oid_get_cipher_alg (头未声明) */
/* mbedtls_oid_get_ec_grp (头未声明) */
/* mbedtls_oid_get_ec_grp_algid (头未声明) */
/* mbedtls_oid_get_md_hmac (头未声明) */
/* mbedtls_oid_get_oid_by_ec_grp (头未声明) */
/* mbedtls_oid_get_oid_by_ec_grp_algid (头未声明) */
/* mbedtls_oid_get_oid_by_md (头未声明) */
/* mbedtls_oid_get_oid_by_pk_alg (头未声明) */
/* mbedtls_oid_get_pk_alg (头未声明) */
/* mbedtls_pk_can_do (头未声明) */
/* mbedtls_pk_can_do_ext (头未声明) */
/* mbedtls_pk_ecc_set_group (头未声明) */
/* mbedtls_pk_ecc_set_key (头未声明) */
/* mbedtls_pk_ecc_set_pubkey (头未声明) */
/* mbedtls_pk_get_type (头未声明) */
/* mbedtls_pk_info_from_type (头未声明) */
/* mbedtls_pk_load_file (头未声明) */
/* mbedtls_pk_parse_subpubkey (头未声明) */
/* mbedtls_pk_psa_rsa_sign_ext (头未声明) */
/* mbedtls_pk_rsa_set_key (头未声明) */
/* mbedtls_pk_rsa_set_pubkey (头未声明) */
/* mbedtls_pk_set_pubkey_from_prv (头未声明) */
/* mbedtls_pk_setup (头未声明) */
/* mbedtls_pk_write_pubkey (头未声明) */
/* mbedtls_pkcs5_pbes2_ext (头未声明) */
/* mbedtls_pkcs5_pbkdf2_hmac_ext (头未声明) */
/* mbedtls_pkcs5_self_test (头未声明) */
/* mbedtls_poly1305_finish (头未声明) */
/* mbedtls_poly1305_free (头未声明) */
/* mbedtls_poly1305_init (头未声明) */
/* mbedtls_poly1305_mac (头未声明) */
/* mbedtls_poly1305_self_test (头未声明) */
/* mbedtls_poly1305_starts (头未声明) */
/* mbedtls_poly1305_update (头未声明) */
/* mbedtls_psa_aead_abort (头未声明) */
/* mbedtls_psa_aead_decrypt (头未声明) */
/* mbedtls_psa_aead_decrypt_setup (头未声明) */
/* mbedtls_psa_aead_encrypt (头未声明) */
/* mbedtls_psa_aead_encrypt_setup (头未声明) */
/* mbedtls_psa_aead_finish (头未声明) */
/* mbedtls_psa_aead_set_lengths (头未声明) */
/* mbedtls_psa_aead_set_nonce (头未声明) */
/* mbedtls_psa_aead_update (头未声明) */
/* mbedtls_psa_aead_update_ad (头未声明) */
/* mbedtls_psa_asymmetric_decrypt (头未声明) */
/* mbedtls_psa_asymmetric_encrypt (头未声明) */
/* mbedtls_psa_cipher_abort (头未声明) */
/* mbedtls_psa_cipher_decrypt (头未声明) */
/* mbedtls_psa_cipher_decrypt_setup (头未声明) */
/* mbedtls_psa_cipher_encrypt (头未声明) */
/* mbedtls_psa_cipher_encrypt_setup (头未声明) */
/* mbedtls_psa_cipher_finish (头未声明) */
/* mbedtls_psa_cipher_set_iv (头未声明) */
/* mbedtls_psa_cipher_update (头未声明) */
/* mbedtls_psa_crypto_configure_entropy_sources (头未声明) */
/* mbedtls_psa_ecdsa_sign_hash (头未声明) */
/* mbedtls_psa_ecdsa_verify_hash (头未声明) */
/* mbedtls_psa_ecp_export_key (头未声明) */
/* mbedtls_psa_ecp_export_public_key (头未声明) */
/* mbedtls_psa_ecp_generate_key (头未声明) */
/* mbedtls_psa_ecp_import_key (头未声明) */
/* mbedtls_psa_ecp_load_public_part (头未声明) */
/* mbedtls_psa_ecp_load_representation (头未声明) */
/* mbedtls_psa_ffdh_export_public_key (头未声明) */
/* mbedtls_psa_ffdh_generate_key (头未声明) */
/* mbedtls_psa_ffdh_import_key (头未声明) */
/* mbedtls_psa_ffdh_key_agreement (头未声明) */
/* mbedtls_psa_hash_abort (头未声明) */
/* mbedtls_psa_hash_clone (头未声明) */
/* mbedtls_psa_hash_compute (头未声明) */
/* mbedtls_psa_hash_finish (头未声明) */
/* mbedtls_psa_hash_setup (头未声明) */
/* mbedtls_psa_hash_update (头未声明) */
/* mbedtls_psa_interruptible_set_max_ops (头未声明) */
/* mbedtls_psa_key_agreement_ecdh (头未声明) */
/* mbedtls_psa_mac_abort (头未声明) */
/* mbedtls_psa_mac_compute (头未声明) */
/* mbedtls_psa_mac_sign_finish (头未声明) */
/* mbedtls_psa_mac_sign_setup (头未声明) */
/* mbedtls_psa_mac_update (头未声明) */
/* mbedtls_psa_mac_verify_finish (头未声明) */
/* mbedtls_psa_mac_verify_setup (头未声明) */
/* mbedtls_psa_pake_abort (头未声明) */
/* mbedtls_psa_pake_get_implicit_key (头未声明) */
/* mbedtls_psa_pake_input (头未声明) */
/* mbedtls_psa_pake_output (头未声明) */
/* mbedtls_psa_pake_setup (头未声明) */
/* mbedtls_psa_rsa_export_key (头未声明) */
/* mbedtls_psa_rsa_export_public_key (头未声明) */
/* mbedtls_psa_rsa_generate_key (头未声明) */
/* mbedtls_psa_rsa_import_key (头未声明) */
/* mbedtls_psa_rsa_load_representation (头未声明) */
/* mbedtls_psa_rsa_sign_hash (头未声明) */
/* mbedtls_psa_rsa_verify_hash (头未声明) */
/* mbedtls_psa_sign_hash_abort (头未声明) */
/* mbedtls_psa_sign_hash_complete (头未声明) */
/* mbedtls_psa_sign_hash_get_num_ops (头未声明) */
/* mbedtls_psa_sign_hash_start (头未声明) */
/* mbedtls_psa_verify_hash_abort (头未声明) */
/* mbedtls_psa_verify_hash_complete (头未声明) */
/* mbedtls_psa_verify_hash_get_num_ops (头未声明) */
/* mbedtls_psa_verify_hash_start (头未声明) */
/* mbedtls_psa_xof_abort (头未声明) */
/* mbedtls_psa_xof_output (头未声明) */
/* mbedtls_psa_xof_set_context (头未声明) */
/* mbedtls_psa_xof_setup (头未声明) */
/* mbedtls_psa_xof_update (头未声明) */
/* mbedtls_ripemd160 (头未声明) */
/* mbedtls_ripemd160_clone (头未声明) */
/* mbedtls_ripemd160_finish (头未声明) */
/* mbedtls_ripemd160_free (头未声明) */
/* mbedtls_ripemd160_init (头未声明) */
/* mbedtls_ripemd160_self_test (头未声明) */
/* mbedtls_ripemd160_starts (头未声明) */
/* mbedtls_ripemd160_update (头未声明) */
/* mbedtls_rsa_check_privkey (头未声明) */
/* mbedtls_rsa_check_pub_priv (头未声明) */
/* mbedtls_rsa_check_pubkey (头未声明) */
/* mbedtls_rsa_copy (头未声明) */
/* mbedtls_rsa_deduce_crt (头未声明) */
/* mbedtls_rsa_deduce_private_exponent (头未声明) */
/* mbedtls_rsa_free (头未声明) */
/* mbedtls_rsa_gen_key (头未声明) */
/* mbedtls_rsa_get_bitlen (头未声明) */
/* mbedtls_rsa_get_len (头未声明) */
/* mbedtls_rsa_get_md_alg (头未声明) */
/* mbedtls_rsa_get_padding_mode (头未声明) */
/* mbedtls_rsa_init (头未声明) */
/* mbedtls_rsa_parse_key (头未声明) */
/* mbedtls_rsa_parse_pubkey (头未声明) */
/* mbedtls_rsa_pkcs1_decrypt (头未声明) */
/* mbedtls_rsa_pkcs1_encrypt (头未声明) */
/* mbedtls_rsa_pkcs1_sign (头未声明) */
/* mbedtls_rsa_pkcs1_verify (头未声明) */
/* mbedtls_rsa_private (头未声明) */
/* mbedtls_rsa_public (头未声明) */
/* mbedtls_rsa_rsaes_oaep_decrypt (头未声明) */
/* mbedtls_rsa_rsaes_oaep_encrypt (头未声明) */
/* mbedtls_rsa_rsaes_pkcs1_v15_decrypt (头未声明) */
/* mbedtls_rsa_rsaes_pkcs1_v15_decrypt_ext (头未声明) */
/* mbedtls_rsa_rsaes_pkcs1_v15_encrypt (头未声明) */
/* mbedtls_rsa_rsassa_pkcs1_v15_sign (头未声明) */
/* mbedtls_rsa_rsassa_pkcs1_v15_verify (头未声明) */
/* mbedtls_rsa_rsassa_pss_sign_ext (头未声明) */
/* mbedtls_rsa_rsassa_pss_sign_no_mode_check (头未声明) */
/* mbedtls_rsa_rsassa_pss_verify_ext (头未声明) */
/* mbedtls_rsa_self_test (头未声明) */
/* mbedtls_rsa_set_padding (头未声明) */
/* mbedtls_rsa_validate_crt (头未声明) */
/* mbedtls_rsa_validate_params (头未声明) */
/* mbedtls_rsa_write_key (头未声明) */
/* mbedtls_rsa_write_pubkey (头未声明) */
/* mbedtls_sha1 (头未声明) */
/* mbedtls_sha1_clone (头未声明) */
/* mbedtls_sha1_finish (头未声明) */
/* mbedtls_sha1_free (头未声明) */
/* mbedtls_sha1_init (头未声明) */
/* mbedtls_sha1_self_test (头未声明) */
/* mbedtls_sha1_starts (头未声明) */
/* mbedtls_sha1_update (头未声明) */
/* mbedtls_sha224_self_test (头未声明) */
/* mbedtls_sha256 (头未声明) */
/* mbedtls_sha256_clone (头未声明) */
/* mbedtls_sha256_finish (头未声明) */
/* mbedtls_sha256_free (头未声明) */
/* mbedtls_sha256_init (头未声明) */
/* mbedtls_sha256_self_test (头未声明) */
/* mbedtls_sha256_starts (头未声明) */
/* mbedtls_sha256_update (头未声明) */
/* mbedtls_sha3 (头未声明) */
/* mbedtls_sha384_self_test (头未声明) */
/* mbedtls_sha3_clone (头未声明) */
/* mbedtls_sha3_finish (头未声明) */
/* mbedtls_sha3_free (头未声明) */
/* mbedtls_sha3_init (头未声明) */
/* mbedtls_sha3_self_test (头未声明) */
/* mbedtls_sha3_starts (头未声明) */
/* mbedtls_sha3_update (头未声明) */
/* mbedtls_sha512 (头未声明) */
/* mbedtls_sha512_clone (头未声明) */
/* mbedtls_sha512_finish (头未声明) */
/* mbedtls_sha512_free (头未声明) */
/* mbedtls_sha512_init (头未声明) */
/* mbedtls_sha512_self_test (头未声明) */
/* mbedtls_sha512_starts (头未声明) */
/* mbedtls_sha512_update (头未声明) */
/* mbedtls_ssl_add_hs_hdr_to_checksum (头未声明) */
/* mbedtls_ssl_add_hs_msg_to_checksum (头未声明) */
/* mbedtls_ssl_buffering_free (头未声明) */
/* mbedtls_ssl_check_cert_usage (头未声明) */
/* mbedtls_ssl_check_curve (头未声明) */
/* mbedtls_ssl_check_curve_tls_id (头未声明) */
/* mbedtls_ssl_check_timer (头未声明) */
/* mbedtls_ssl_cipher_to_psa (头未声明) */
/* mbedtls_ssl_ciphersuite_uses_ec (头未声明) */
/* mbedtls_ssl_ciphersuite_uses_psk (头未声明) */
/* mbedtls_ssl_conf_has_static_psk (头未声明) */
/* mbedtls_ssl_cookie_check (头未声明) */
/* mbedtls_ssl_cookie_write (头未声明) */
/* mbedtls_ssl_decrypt_buf (头未声明) */
/* mbedtls_ssl_derive_keys (头未声明) */
/* mbedtls_ssl_dtls_replay_check (头未声明) */
/* mbedtls_ssl_dtls_replay_reset (头未声明) */
/* mbedtls_ssl_dtls_replay_update (头未声明) */
/* mbedtls_ssl_encrypt_buf (头未声明) */
/* mbedtls_ssl_fetch_input (头未声明) */
/* mbedtls_ssl_finish_handshake_msg (头未声明) */
/* mbedtls_ssl_flight_free (头未声明) */
/* mbedtls_ssl_flight_transmit (头未声明) */
/* mbedtls_ssl_flush_output (头未声明) */
/* mbedtls_ssl_get_ciphersuite_sig_alg (头未声明) */
/* mbedtls_ssl_get_ciphersuite_sig_pk_alg (头未声明) */
/* mbedtls_ssl_get_ciphersuite_sig_pk_psa_alg (头未声明) */
/* mbedtls_ssl_get_ciphersuite_sig_pk_psa_usage (头未声明) */
/* mbedtls_ssl_get_current_mtu (头未声明) */
/* mbedtls_ssl_get_curve_name_from_tls_id (头未声明) */
/* mbedtls_ssl_get_ecp_group_id_from_tls_id (头未声明) */
/* mbedtls_ssl_get_extension_id (头未声明) */
/* mbedtls_ssl_get_extension_mask (头未声明) */
/* mbedtls_ssl_get_extension_name (头未声明) */
/* mbedtls_ssl_get_handshake_transcript (头未声明) */
/* mbedtls_ssl_get_hs_msg_name (头未声明) */
/* mbedtls_ssl_get_input_max_frag_len (头未声明) */
/* mbedtls_ssl_get_key_exchange_md_tls1_2 (头未声明) */
/* mbedtls_ssl_get_mode_from_ciphersuite (头未声明) */
/* mbedtls_ssl_get_mode_from_transform (头未声明) */
/* mbedtls_ssl_get_output_max_frag_len (头未声明) */
/* mbedtls_ssl_get_psa_curve_info_from_tls_id (头未声明) */
/* mbedtls_ssl_get_tls_id_from_ecp_group_id (头未声明) */
/* mbedtls_ssl_handle_message_type (头未声明) */
/* mbedtls_ssl_handle_pending_alert (头未声明) */
/* mbedtls_ssl_handshake_client_step (头未声明) */
/* mbedtls_ssl_handshake_free (头未声明) */
/* mbedtls_ssl_handshake_server_step (头未声明) */
/* mbedtls_ssl_handshake_wrapup (头未声明) */
/* mbedtls_ssl_handshake_wrapup_free_hs_transform (头未声明) */
/* mbedtls_ssl_hash_from_md_alg (头未声明) */
/* mbedtls_ssl_key_export_type_str (头未声明) */
/* mbedtls_ssl_md_alg_from_hash (头未声明) */
/* mbedtls_ssl_named_group_to_str (头未声明) */
/* mbedtls_ssl_optimize_checksum (头未声明) */
/* mbedtls_ssl_parse_alpn_ext (头未声明) */
/* mbedtls_ssl_parse_certificate (头未声明) */
/* mbedtls_ssl_parse_change_cipher_spec (头未声明) */
/* mbedtls_ssl_parse_finished (头未声明) */
/* mbedtls_ssl_parse_server_name_ext (头未声明) */
/* mbedtls_ssl_parse_sig_alg_ext (头未声明) */
/* mbedtls_ssl_pend_fatal_alert (头未声明) */
/* mbedtls_ssl_pk_sig_alg_from_sig (头未声明) */
/* mbedtls_ssl_prepare_handshake_record (头未声明) */
/* mbedtls_ssl_print_extension (头未声明) */
/* mbedtls_ssl_print_extensions (头未声明) */
/* mbedtls_ssl_print_ticket_flags (头未声明) */
/* mbedtls_ssl_protocol_version_str (头未声明) */
/* mbedtls_ssl_read_record (头未声明) */
/* mbedtls_ssl_read_version (头未声明) */
/* mbedtls_ssl_recv_flight_completed (头未声明) */
/* mbedtls_ssl_resend (头未声明) */
/* mbedtls_ssl_resend_hello_request (头未声明) */
/* mbedtls_ssl_reset_checksum (头未声明) */
/* mbedtls_ssl_reset_in_pointers (头未声明) */
/* mbedtls_ssl_reset_out_pointers (头未声明) */
/* mbedtls_ssl_reset_transcript_for_hrr (头未声明) */
/* mbedtls_ssl_send_fatal_handshake_failure (头未声明) */
/* mbedtls_ssl_send_flight_completed (头未声明) */
/* mbedtls_ssl_session_copy (头未声明) */
/* mbedtls_ssl_session_reset_int (头未声明) */
/* mbedtls_ssl_session_reset_msg_layer (头未声明) */
/* mbedtls_ssl_session_set_hostname (头未声明) */
/* mbedtls_ssl_set_calc_verify_md (头未声明) */
/* mbedtls_ssl_set_inbound_transform (头未声明) */
/* mbedtls_ssl_set_outbound_transform (头未声明) */
/* mbedtls_ssl_set_timer (头未声明) */
/* mbedtls_ssl_sig_alg_to_str (头未声明) */
/* mbedtls_ssl_sig_from_pk (头未声明) */
/* mbedtls_ssl_sig_from_pk_alg (头未声明) */
/* mbedtls_ssl_start_handshake_msg (头未声明) */
/* mbedtls_ssl_start_renegotiation (头未声明) */
/* mbedtls_ssl_states_str (头未声明) */
/* mbedtls_ssl_ticket_parse (头未声明) */
/* mbedtls_ssl_ticket_write (头未声明) */
/* mbedtls_ssl_tls12_get_preferred_hash_for_sig_alg (头未声明) */
/* mbedtls_ssl_tls12_write_client_hello_exts (头未声明) */
/* mbedtls_ssl_tls13_calculate_verify_data (头未声明) */
/* mbedtls_ssl_tls13_check_received_extension (头未声明) */
/* mbedtls_ssl_tls13_check_sig_alg_cert_key_match (头未声明) */
/* mbedtls_ssl_tls13_compute_application_transform (头未声明) */
/* mbedtls_ssl_tls13_compute_handshake_transform (头未声明) */
/* mbedtls_ssl_tls13_compute_resumption_master_secret (头未声明) */
/* mbedtls_ssl_tls13_create_psk_binder (头未声明) */
/* mbedtls_ssl_tls13_derive_application_secrets (头未声明) */
/* mbedtls_ssl_tls13_derive_early_secrets (头未声明) */
/* mbedtls_ssl_tls13_derive_handshake_secrets (头未声明) */
/* mbedtls_ssl_tls13_derive_resumption_master_secret (头未声明) */
/* mbedtls_ssl_tls13_derive_secret (头未声明) */
/* mbedtls_ssl_tls13_evolve_secret (头未声明) */
/* mbedtls_ssl_tls13_export_handshake_psk (头未声明) */
/* mbedtls_ssl_tls13_exporter (头未声明) */
/* mbedtls_ssl_tls13_fetch_handshake_msg (头未声明) */
/* mbedtls_ssl_tls13_finalize_client_hello (头未声明) */
/* mbedtls_ssl_tls13_generate_and_write_xxdh_key_exchange (头未声明) */
/* mbedtls_ssl_tls13_handshake_client_step (头未声明) */
/* mbedtls_ssl_tls13_handshake_server_step (头未声明) */
/* mbedtls_ssl_tls13_handshake_wrapup (头未声明) */
/* mbedtls_ssl_tls13_hkdf_expand_label (头未声明) */
/* mbedtls_ssl_tls13_is_supported_versions_ext_present_in_exts (头未声明) */
/* mbedtls_ssl_tls13_key_schedule_stage_early (头未声明) */
/* mbedtls_ssl_tls13_make_traffic_keys (头未声明) */
/* mbedtls_ssl_tls13_populate_transform (头未声明) */
/* mbedtls_ssl_tls13_process_certificate (头未声明) */
/* mbedtls_ssl_tls13_process_certificate_verify (头未声明) */
/* mbedtls_ssl_tls13_process_finished_message (头未声明) */
/* mbedtls_ssl_tls13_read_public_xxdhe_share (头未声明) */
/* mbedtls_ssl_tls13_write_binders_of_pre_shared_key_ext (头未声明) */
/* mbedtls_ssl_tls13_write_certificate (头未声明) */
/* mbedtls_ssl_tls13_write_certificate_verify (头未声明) */
/* mbedtls_ssl_tls13_write_change_cipher_spec (头未声明) */
/* mbedtls_ssl_tls13_write_client_hello_exts (头未声明) */
/* mbedtls_ssl_tls13_write_finished_message (头未声明) */
/* mbedtls_ssl_tls13_write_identities_of_pre_shared_key_ext (头未声明) */
/* mbedtls_ssl_transform_free (头未声明) */
/* mbedtls_ssl_transform_init (头未声明) */
/* mbedtls_ssl_update_handshake_status (头未声明) */
/* mbedtls_ssl_update_in_pointers (头未声明) */
/* mbedtls_ssl_update_out_pointers (头未声明) */
/* mbedtls_ssl_validate_ciphersuite (头未声明) */
/* mbedtls_ssl_verify_certificate (头未声明) */
/* mbedtls_ssl_write_alpn_ext (头未声明) */
/* mbedtls_ssl_write_certificate (头未声明) */
/* mbedtls_ssl_write_change_cipher_spec (头未声明) */
/* mbedtls_ssl_write_client_hello (头未声明) */
/* mbedtls_ssl_write_finished (头未声明) */
/* mbedtls_ssl_write_handshake_msg_ext (头未声明) */
/* mbedtls_ssl_write_record (头未声明) */
/* mbedtls_ssl_write_sig_alg_ext (头未声明) */
/* mbedtls_ssl_write_version (头未声明) */
/* mbedtls_tls_prf_types_str (头未声明) */
/* mbedtls_to_psa_error (头未声明) */
/* mbedtls_x509_get_alg (头未声明) */
/* mbedtls_x509_get_alg_null (头未声明) */
/* mbedtls_x509_get_ext (头未声明) */
/* mbedtls_x509_get_key_usage (头未声明) */
/* mbedtls_x509_get_name (头未声明) */
/* mbedtls_x509_get_ns_cert_type (头未声明) */
/* mbedtls_x509_get_rsassa_pss_params (头未声明) */
/* mbedtls_x509_get_serial (头未声明) */
/* mbedtls_x509_get_sig (头未声明) */
/* mbedtls_x509_get_sig_alg (头未声明) */
/* mbedtls_x509_get_subject_alt_name (头未声明) */
/* mbedtls_x509_get_subject_alt_name_ext (头未声明) */
/* mbedtls_x509_get_time (头未声明) */
/* mbedtls_x509_info_cert_type (头未声明) */
/* mbedtls_x509_info_key_usage (头未声明) */
/* mbedtls_x509_info_subject_alt_name (头未声明) */
/* mbedtls_x509_key_size_helper (头未声明) */
/* mbedtls_x509_oid_get_attr_short_name (头未声明) */
/* mbedtls_x509_oid_get_certificate_policies (头未声明) */
/* mbedtls_x509_oid_get_extended_key_usage (头未声明) */
/* mbedtls_x509_oid_get_md_alg (头未声明) */
/* mbedtls_x509_oid_get_oid_by_sig_alg (头未声明) */
/* mbedtls_x509_oid_get_sig_alg (头未声明) */
/* mbedtls_x509_oid_get_sig_alg_desc (头未声明) */
/* mbedtls_x509_oid_get_x509_ext_type (头未声明) */
/* mbedtls_x509_profile_check_md_alg (头未声明) */
/* mbedtls_x509_profile_check_pk_alg (头未声明) */
/* mbedtls_x509_set_extension (头未声明) */
/* mbedtls_x509_sig_alg_gets (头未声明) */
/* mbedtls_x509_write_extensions (头未声明) */
/* mbedtls_x509_write_names (头未声明) */
/* mbedtls_x509_write_set_san_common (头未声明) */
/* mbedtls_x509_write_sig (头未声明) */
/* mbedtls_zeroize_and_free (头未声明) */
/* psa_allocate_buffer_to_slot (头未声明) */
/* psa_copy_key_material_into_slot (头未声明) */
/* psa_crypto_local_input_alloc (头未声明) */
/* psa_crypto_local_input_free (头未声明) */
/* psa_crypto_local_output_alloc (头未声明) */
/* psa_crypto_local_output_free (头未声明) */
/* psa_custom_key_parameters_are_default (头未声明) */
/* psa_destroy_persistent_key (头未声明) */
/* psa_driver_wrapper_export_public_key (头未声明) */
/* psa_driver_wrapper_get_builtin_key (头未声明) */
/* psa_driver_wrapper_get_key_buffer_size (头未声明) */
/* psa_export_key_internal (头未声明) */
/* psa_export_public_key_internal (头未声明) */
/* psa_format_key_data_for_storage (头未声明) */
/* psa_free_key_slot (头未声明) */
/* psa_free_persistent_key_data (头未声明) */
/* psa_generate_key_internal (头未声明) */
/* psa_generic_status_to_mbedtls (头未声明) */
/* psa_get_and_lock_key_slot (头未声明) */
/* psa_import_key_into_slot (头未声明) */
/* psa_initialize_key_slots (头未声明) */
/* psa_is_key_present_in_storage (头未声明) */
/* psa_is_ready_for_cipher (头未声明) */
/* psa_its_get (头未声明) */
/* psa_its_get_info (头未声明) */
/* psa_its_remove (头未声明) */
/* psa_its_set (头未声明) */
/* psa_key_agreement_raw_builtin (头未声明) */
/* psa_load_persistent_key (头未声明) */
/* psa_parse_key_data_from_storage (头未声明) */
/* psa_pk_status_to_mbedtls (头未声明) */
/* psa_random_internal_free (头未声明) */
/* psa_random_internal_generate (头未声明) */
/* psa_random_internal_init (头未声明) */
/* psa_random_internal_seed (头未声明) */
/* psa_remove_key_data_from_memory (头未声明) */
/* psa_reserve_free_key_slot (头未声明) */
/* psa_save_persistent_key (头未声明) */
/* psa_sign_hash_builtin (头未声明) */
/* psa_sign_message_builtin (头未声明) */
/* psa_status_to_mbedtls (头未声明) */
/* psa_unregister_read (头未声明) */
/* psa_unregister_read_under_mutex (头未声明) */
/* psa_validate_key_persistence (头未声明) */
/* psa_validate_unstructured_key_bit_size (头未声明) */
/* psa_verify_hash_builtin (头未声明) */
/* psa_verify_message_builtin (头未声明) */
/* psa_wipe_all_key_slots (头未声明) */
/* psa_wipe_key_slot (头未声明) */

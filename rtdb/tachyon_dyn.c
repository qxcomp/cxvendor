/* ============================================================
 * tachyon_dyn.c - dlopen 模拟链接：Tachyon C API
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照上游 core/include/tachyon.h 的 36 个
 * TACHYON_ABI 公开函数、2 个 opaque handle、2 个枚举和消息视图
 * 结构体；导出函数名保持原生原样。
 * 链接期不需要 libtachyon，仅需 -ldl。
 * 加载顺序：显式路径 > TACHYON_DYN_LIB > LD_LIBRARY_PATH > .so >
 *           Nix > 平台默认；成功后不卸载。
 * 加载日志照搬 opendal_dyn.c 当前 diff。
 * ============================================================ */

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ---------- 平台适配层 ---------- */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE tachyon_lib_handle_t;
  static tachyon_lib_handle_t tachyon_load_lib(const char *path) { return LoadLibraryA(path); }
  static void *tachyon_get_sym(tachyon_lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
  static void tachyon_free_lib(tachyon_lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *tachyon_lib_error(void) { return "LoadLibrary failed"; }
  #define TACHYON_PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *tachyon_lib_handle_t;
  static tachyon_lib_handle_t tachyon_load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
  static void *tachyon_get_sym(tachyon_lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void tachyon_free_lib(tachyon_lib_handle_t h) { if (h) dlclose(h); }
  static const char *tachyon_lib_error(void) { return dlerror(); }
  #define TACHYON_PATH_SEP ':'
#endif

/* ---------- 类型（core/include/tachyon.h 原样内嵌） ---------- */
typedef enum {
    TACHYON_SUCCESS           = 0,
    TACHYON_ERR_NULL_PTR      = 1,
    TACHYON_ERR_MEM           = 2,
    TACHYON_ERR_OPEN          = 3,
    TACHYON_ERR_TRUNCATE      = 4,
    TACHYON_ERR_CHMOD         = 5,
    TACHYON_ERR_SEAL          = 6,
    TACHYON_ERR_MAP           = 7,
    TACHYON_ERR_INVALID_SZ    = 8,
    TACHYON_ERR_FULL          = 9,
    TACHYON_ERR_EMPTY         = 10,
    TACHYON_ERR_NETWORK       = 11,
    TACHYON_ERR_SYSTEM        = 12,
    TACHYON_ERR_INTERRUPTED   = 13,
    TACHYON_ERR_ABI_MISMATCH  = 14
} tachyon_error_t;

typedef enum {
    TACHYON_STATE_UNINITIALIZED = 0,
    TACHYON_STATE_INITIALIZING  = 1,
    TACHYON_STATE_READY         = 2,
    TACHYON_STATE_DISCONNECTED  = 3,
    TACHYON_STATE_FATAL_ERROR   = 4,
    TACHYON_STATE_UNKNOWN       = 5
} tachyon_state_t;

typedef struct tachyon_bus tachyon_bus_t;
typedef struct tachyon_rpc_bus tachyon_rpc_bus_t;

typedef struct {
    const void *ptr;
    size_t actual_size;
    size_t reserved_;
    uint32_t type_id;
    uint32_t padding_;
} tachyon_msg_view_t;

#define TACHYON_TYPE_ID(route, type) (((uint32_t)(route) << 16) | (uint32_t)(type))
#define TACHYON_ROUTE_ID(type_id) ((uint16_t)((type_id) >> 16))
#define TACHYON_MSG_TYPE(type_id) ((uint16_t)((type_id) & 0xFFFF))

/* ---------- 动态层状态 ---------- */
static tachyon_lib_handle_t tachyon_handle = NULL;
static int tachyon_init_state = 0;

static void *tachyon_sym(const char *name) {
    return tachyon_handle ? tachyon_get_sym(tachyon_handle, name) : NULL;
}

static int tachyon_do_load(const char *library_path) {
    static char bufs[64][1024];
    const char *candidates[64];
    const char *so_plain = "libtachyon.so";
    int n = 0, i;
    int res = -1;

    if (tachyon_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("TACHYON_DYN_LIB");
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
                while (q && *q && n < 52) {
                    char *sep = strchr(q, TACHYON_PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q && n + 1 < 64) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/%s", q, so_plain);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = so_plain;
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/%s", home, so_plain);
            candidates[n] = bufs[n]; n++;
        }
        snprintf(bufs[n], sizeof bufs[0], "/nix/var/nix/profiles/default/lib/%s", so_plain);
        candidates[n] = bufs[n]; n++;
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libtachyon.dylib";
    candidates[n++] = "/usr/local/lib/libtachyon.dylib";
#elif defined(_WIN32)
    candidates[n++] = "tachyon.dll";
    candidates[n++] = "libtachyon.dll";
#else
    snprintf(bufs[n], sizeof bufs[0], "/usr/local/lib/%s", so_plain);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/%s", so_plain);
    candidates[n] = bufs[n]; n++;
    snprintf(bufs[n], sizeof bufs[0], "/usr/lib/x86_64-linux-gnu/%s", so_plain);
    candidates[n] = bufs[n]; n++;
#endif

    for (i = 0; i < n; i++) {
        tachyon_handle = tachyon_load_lib(candidates[i]);
        if (tachyon_handle) { res = i; break; }
    }
    if (!tachyon_handle || !tachyon_sym("tachyon_bus_connect")) {
        fprintf(stderr, "tachyon_dyn: cannot load tachyon (%s)\n", tachyon_lib_error());
        if (tachyon_handle) { tachyon_free_lib(tachyon_handle); tachyon_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "tachyon_dyn: loaded %d %s\n", res, candidates[res]);
    return 0;
}

int tachyon_libok(void) {
    if (tachyon_init_state == 0)
        tachyon_init_state = (tachyon_do_load(NULL) == 0) ? 1 : -1;
    return (tachyon_init_state == 1 && tachyon_handle != NULL) ? 1 : 0;
}

int tachyon_dyn_init(const char *library_path) {
    if (tachyon_do_load(library_path) == 0) {
        tachyon_init_state = 1;
        return 0;
    }
    if (tachyon_init_state == 0) tachyon_init_state = -1;
    return -1;
}

void tachyon_dyn_cleanup(void) {
    if (tachyon_handle) { tachyon_free_lib(tachyon_handle); tachyon_handle = NULL; }
    tachyon_init_state = 0;
}

int tachyon_dyn_has_symbol(const char *name) {
    if (!tachyon_libok()) return 0;
    return tachyon_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define TACHYON_WRAP_RET(ret, name, params, args, fallback)                            \
ret name params {                                                              \
    static ret (*fn) params = NULL;                                            \
    if (!fn) {                                                                 \
        if (!tachyon_libok()) return fallback;                                 \
        *(void **)(&fn) = tachyon_sym(#name);                                  \
        if (!fn) {                                                             \
            fprintf(stderr, "tachyon_dyn: dlsym failed: %s\n", #name);         \
            return fallback;                                                   \
        }                                                                      \
    }                                                                          \
    return fn args;                                                            \
}
#define TACHYON_WRAP_VOID(name, params, args)                                          \
void name params {                                                             \
    static void (*fn) params = NULL;                                           \
    if (!fn) {                                                                 \
        if (!tachyon_libok()) return;                                          \
        *(void **)(&fn) = tachyon_sym(#name);                                  \
        if (!fn) {                                                             \
            fprintf(stderr, "tachyon_dyn: dlsym failed: %s\n", #name);         \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    fn args;                                                                   \
}

/* ---------- 36 个 TACHYON_ABI 公开函数（函数名原样） ---------- */
TACHYON_WRAP_VOID(tachyon_memory_barrier_acquire, (void), ())
TACHYON_WRAP_RET(tachyon_error_t, tachyon_bus_listen, (const char *socket_path, size_t capacity, tachyon_bus_t **out_bus), (socket_path, capacity, out_bus), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_RET(tachyon_error_t, tachyon_bus_connect, (const char *socket_path, tachyon_bus_t **out_bus), (socket_path, out_bus), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_VOID(tachyon_bus_ref, (tachyon_bus_t *bus), (bus))
TACHYON_WRAP_VOID(tachyon_bus_destroy, (tachyon_bus_t *bus), (bus))
TACHYON_WRAP_RET(tachyon_error_t, tachyon_bus_set_numa_node, (const tachyon_bus_t *bus, int node_id), (bus, node_id), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_RET(void *, tachyon_acquire_tx, (tachyon_bus_t *bus, size_t max_payload_size), (bus, max_payload_size), NULL)
TACHYON_WRAP_RET(tachyon_error_t, tachyon_commit_tx, (tachyon_bus_t *bus, size_t actual_payload_size, uint32_t type_id), (bus, actual_payload_size, type_id), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_RET(tachyon_error_t, tachyon_rollback_tx, (tachyon_bus_t *bus), (bus), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_RET(const void *, tachyon_acquire_rx, (tachyon_bus_t *bus, uint32_t *out_type_id, size_t *out_actual_size), (bus, out_type_id, out_actual_size), NULL)
TACHYON_WRAP_RET(const void *, tachyon_acquire_rx_spin, (tachyon_bus_t *bus, uint32_t *out_type_id, size_t *out_actual_size, uint32_t max_spins), (bus, out_type_id, out_actual_size, max_spins), NULL)
TACHYON_WRAP_RET(const void *, tachyon_acquire_rx_blocking, (tachyon_bus_t *bus, uint32_t *out_type_id, size_t *out_actual_size, uint32_t spin_threshold), (bus, out_type_id, out_actual_size, spin_threshold), NULL)
TACHYON_WRAP_RET(tachyon_error_t, tachyon_commit_rx, (tachyon_bus_t *bus), (bus), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_RET(size_t, tachyon_acquire_rx_batch, (tachyon_bus_t *bus, tachyon_msg_view_t *out_views, size_t max_msgs), (bus, out_views, max_msgs), 0)
TACHYON_WRAP_RET(size_t, tachyon_drain_batch, (tachyon_bus_t *bus, tachyon_msg_view_t *out_views, size_t max_msgs, uint32_t spin_threshold), (bus, out_views, max_msgs, spin_threshold), 0)
TACHYON_WRAP_RET(tachyon_error_t, tachyon_commit_rx_batch, (tachyon_bus_t *bus, const tachyon_msg_view_t *views, size_t count), (bus, views, count), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_VOID(tachyon_bus_set_polling_mode, (const tachyon_bus_t *bus, int pure_spin), (bus, pure_spin))
TACHYON_WRAP_VOID(tachyon_flush, (tachyon_bus_t *bus), (bus))
TACHYON_WRAP_RET(tachyon_state_t, tachyon_get_state, (const tachyon_bus_t *bus), (bus), TACHYON_STATE_UNKNOWN)
TACHYON_WRAP_RET(tachyon_error_t, tachyon_rpc_listen, (const char *socket_path, size_t cap_fwd, size_t cap_rev, tachyon_rpc_bus_t **out_rpc), (socket_path, cap_fwd, cap_rev, out_rpc), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_RET(tachyon_error_t, tachyon_rpc_connect, (const char *socket_path, tachyon_rpc_bus_t **out_rpc), (socket_path, out_rpc), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_VOID(tachyon_rpc_destroy, (tachyon_rpc_bus_t *rpc), (rpc))
TACHYON_WRAP_RET(tachyon_error_t, tachyon_rpc_call, (tachyon_rpc_bus_t *rpc, const void *payload, size_t size, uint32_t msg_type, uint64_t *out_correlation_id), (rpc, payload, size, msg_type, out_correlation_id), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_RET(const void *, tachyon_rpc_wait, (tachyon_rpc_bus_t *rpc, uint64_t correlation_id, size_t *out_size, uint32_t *out_msg_type, uint32_t spin_threshold), (rpc, correlation_id, out_size, out_msg_type, spin_threshold), NULL)
TACHYON_WRAP_RET(tachyon_error_t, tachyon_rpc_commit_rx, (tachyon_rpc_bus_t *rpc), (rpc), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_RET(const void *, tachyon_rpc_serve, (tachyon_rpc_bus_t *rpc, uint64_t *out_correlation_id, uint32_t *out_msg_type, size_t *out_size, uint32_t spin_threshold), (rpc, out_correlation_id, out_msg_type, out_size, spin_threshold), NULL)
TACHYON_WRAP_RET(tachyon_error_t, tachyon_rpc_commit_serve, (tachyon_rpc_bus_t *rpc), (rpc), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_RET(tachyon_error_t, tachyon_rpc_reply, (tachyon_rpc_bus_t *rpc, uint64_t correlation_id, const void *payload, size_t size, uint32_t msg_type), (rpc, correlation_id, payload, size, msg_type), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_RET(void *, tachyon_rpc_acquire_tx, (tachyon_rpc_bus_t *rpc, size_t max_size), (rpc, max_size), NULL)
TACHYON_WRAP_RET(tachyon_error_t, tachyon_rpc_commit_call, (tachyon_rpc_bus_t *rpc, size_t actual_size, uint32_t msg_type, uint64_t *out_cid), (rpc, actual_size, msg_type, out_cid), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_VOID(tachyon_rpc_rollback_call, (tachyon_rpc_bus_t *rpc), (rpc))
TACHYON_WRAP_RET(void *, tachyon_rpc_acquire_reply_tx, (tachyon_rpc_bus_t *rpc, size_t max_size), (rpc, max_size), NULL)
TACHYON_WRAP_RET(tachyon_error_t, tachyon_rpc_commit_reply, (tachyon_rpc_bus_t *rpc, uint64_t cid, size_t actual_size, uint32_t msg_type), (rpc, cid, actual_size, msg_type), TACHYON_ERR_SYSTEM)
TACHYON_WRAP_VOID(tachyon_rpc_rollback_reply, (tachyon_rpc_bus_t *rpc), (rpc))
TACHYON_WRAP_VOID(tachyon_rpc_set_polling_mode, (const tachyon_rpc_bus_t *rpc, int pure_spin), (rpc, pure_spin))
TACHYON_WRAP_RET(tachyon_state_t, tachyon_rpc_get_state, (const tachyon_rpc_bus_t *rpc), (rpc), TACHYON_STATE_UNKNOWN)

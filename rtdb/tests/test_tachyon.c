/* rtdb/tests/test_tachyon.c - Tachyon C API 动态封装定向测试 */
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/* ---------- 类型（不引入目标库头文件） ---------- */
typedef enum {
    TACHYON_SUCCESS          = 0,
    TACHYON_ERR_NULL_PTR     = 1,
    TACHYON_ERR_MEM          = 2,
    TACHYON_ERR_OPEN         = 3,
    TACHYON_ERR_TRUNCATE     = 4,
    TACHYON_ERR_CHMOD        = 5,
    TACHYON_ERR_SEAL         = 6,
    TACHYON_ERR_MAP          = 7,
    TACHYON_ERR_INVALID_SZ   = 8,
    TACHYON_ERR_FULL         = 9,
    TACHYON_ERR_EMPTY        = 10,
    TACHYON_ERR_NETWORK      = 11,
    TACHYON_ERR_SYSTEM       = 12,
    TACHYON_ERR_INTERRUPTED  = 13,
    TACHYON_ERR_ABI_MISMATCH = 14
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

/* ---------- 动态层接口 ---------- */
extern int tachyon_dyn_init(const char *);
extern int tachyon_libok(void);
extern void tachyon_dyn_cleanup(void);
extern int tachyon_dyn_has_symbol(const char *);

/* ---------- 36 个公开函数的原生声明 ---------- */
extern void tachyon_memory_barrier_acquire(void);
extern tachyon_error_t tachyon_bus_listen(const char *socket_path, size_t capacity, tachyon_bus_t **out_bus);
extern tachyon_error_t tachyon_bus_connect(const char *socket_path, tachyon_bus_t **out_bus);
extern void tachyon_bus_ref(tachyon_bus_t *bus);
extern void tachyon_bus_destroy(tachyon_bus_t *bus);
extern tachyon_error_t tachyon_bus_set_numa_node(const tachyon_bus_t *bus, int node_id);
extern void *tachyon_acquire_tx(tachyon_bus_t *bus, size_t max_payload_size);
extern tachyon_error_t tachyon_commit_tx(tachyon_bus_t *bus, size_t actual_payload_size, uint32_t type_id);
extern tachyon_error_t tachyon_rollback_tx(tachyon_bus_t *bus);
extern const void *tachyon_acquire_rx(tachyon_bus_t *bus, uint32_t *out_type_id, size_t *out_actual_size);
extern const void *tachyon_acquire_rx_spin(tachyon_bus_t *bus, uint32_t *out_type_id, size_t *out_actual_size, uint32_t max_spins);
extern const void *tachyon_acquire_rx_blocking(tachyon_bus_t *bus, uint32_t *out_type_id, size_t *out_actual_size, uint32_t spin_threshold);
extern tachyon_error_t tachyon_commit_rx(tachyon_bus_t *bus);
extern size_t tachyon_acquire_rx_batch(tachyon_bus_t *bus, tachyon_msg_view_t *out_views, size_t max_msgs);
extern size_t tachyon_drain_batch(tachyon_bus_t *bus, tachyon_msg_view_t *out_views, size_t max_msgs, uint32_t spin_threshold);
extern tachyon_error_t tachyon_commit_rx_batch(tachyon_bus_t *bus, const tachyon_msg_view_t *views, size_t count);
extern void tachyon_bus_set_polling_mode(const tachyon_bus_t *bus, int pure_spin);
extern void tachyon_flush(tachyon_bus_t *bus);
extern tachyon_state_t tachyon_get_state(const tachyon_bus_t *bus);
extern tachyon_error_t tachyon_rpc_listen(const char *socket_path, size_t cap_fwd, size_t cap_rev, tachyon_rpc_bus_t **out_rpc);
extern tachyon_error_t tachyon_rpc_connect(const char *socket_path, tachyon_rpc_bus_t **out_rpc);
extern void tachyon_rpc_destroy(tachyon_rpc_bus_t *rpc);
extern tachyon_error_t tachyon_rpc_call(tachyon_rpc_bus_t *rpc, const void *payload, size_t size, uint32_t msg_type, uint64_t *out_correlation_id);
extern const void *tachyon_rpc_wait(tachyon_rpc_bus_t *rpc, uint64_t correlation_id, size_t *out_size, uint32_t *out_msg_type, uint32_t spin_threshold);
extern tachyon_error_t tachyon_rpc_commit_rx(tachyon_rpc_bus_t *rpc);
extern const void *tachyon_rpc_serve(tachyon_rpc_bus_t *rpc, uint64_t *out_correlation_id, uint32_t *out_msg_type, size_t *out_size, uint32_t spin_threshold);
extern tachyon_error_t tachyon_rpc_commit_serve(tachyon_rpc_bus_t *rpc);
extern tachyon_error_t tachyon_rpc_reply(tachyon_rpc_bus_t *rpc, uint64_t correlation_id, const void *payload, size_t size, uint32_t msg_type);
extern void *tachyon_rpc_acquire_tx(tachyon_rpc_bus_t *rpc, size_t max_size);
extern tachyon_error_t tachyon_rpc_commit_call(tachyon_rpc_bus_t *rpc, size_t actual_size, uint32_t msg_type, uint64_t *out_cid);
extern void tachyon_rpc_rollback_call(tachyon_rpc_bus_t *rpc);
extern void *tachyon_rpc_acquire_reply_tx(tachyon_rpc_bus_t *rpc, size_t max_size);
extern tachyon_error_t tachyon_rpc_commit_reply(tachyon_rpc_bus_t *rpc, uint64_t cid, size_t actual_size, uint32_t msg_type);
extern void tachyon_rpc_rollback_reply(tachyon_rpc_bus_t *rpc);
extern void tachyon_rpc_set_polling_mode(const tachyon_rpc_bus_t *rpc, int pure_spin);
extern tachyon_state_t tachyon_rpc_get_state(const tachyon_rpc_bus_t *rpc);

static void test_sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;
    const char *names[] = {
        "tachyon_memory_barrier_acquire",
        "tachyon_bus_listen",
        "tachyon_bus_connect",
        "tachyon_bus_ref",
        "tachyon_bus_destroy",
        "tachyon_bus_set_numa_node",
        "tachyon_acquire_tx",
        "tachyon_commit_tx",
        "tachyon_rollback_tx",
        "tachyon_acquire_rx",
        "tachyon_acquire_rx_spin",
        "tachyon_acquire_rx_blocking",
        "tachyon_commit_rx",
        "tachyon_acquire_rx_batch",
        "tachyon_drain_batch",
        "tachyon_commit_rx_batch",
        "tachyon_bus_set_polling_mode",
        "tachyon_flush",
        "tachyon_get_state",
        "tachyon_rpc_listen",
        "tachyon_rpc_connect",
        "tachyon_rpc_destroy",
        "tachyon_rpc_call",
        "tachyon_rpc_wait",
        "tachyon_rpc_commit_rx",
        "tachyon_rpc_serve",
        "tachyon_rpc_commit_serve",
        "tachyon_rpc_reply",
        "tachyon_rpc_acquire_tx",
        "tachyon_rpc_commit_call",
        "tachyon_rpc_rollback_call",
        "tachyon_rpc_acquire_reply_tx",
        "tachyon_rpc_commit_reply",
        "tachyon_rpc_rollback_reply",
        "tachyon_rpc_set_polling_mode",
        "tachyon_rpc_get_state"
    };

    if (tachyon_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("tachyon not installed, skipping tachyon_dyn test\n");

    errs += CHECK(tachyon_libok());
    errs += CHECK(!tachyon_dyn_has_symbol("tachyon_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (tachyon_dyn_has_symbol(names[i])) found++;
    }
    printf("info: tachyon public exports found = %u/%u\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    errs += CHECK(found == sizeof names / sizeof names[0]);

    errs += CHECK(TACHYON_SUCCESS == 0 && TACHYON_ERR_ABI_MISMATCH == 14);
    errs += CHECK(TACHYON_STATE_UNINITIALIZED == 0 && TACHYON_STATE_UNKNOWN == 5);
    errs += CHECK(TACHYON_TYPE_ID(7, 3) == 0x00070003);
    errs += CHECK(TACHYON_ROUTE_ID(TACHYON_TYPE_ID(7, 3)) == 7);
    errs += CHECK(TACHYON_MSG_TYPE(TACHYON_TYPE_ID(7, 3)) == 3);
    tachyon_memory_barrier_acquire();
    printf("ok: tachyon_memory_barrier_acquire() returned\n");

    {
        char path[160];
        pid_t pid;
        int status = 0;
        uint32_t type_id = TACHYON_TYPE_ID(7, 3);

        snprintf(path, sizeof path, "/tmp/opencode/rtdb_tachyon_%ld.sock", (long)getpid());
        unlink(path);
        pid = fork();
        if (pid == 0) {
            tachyon_bus_t *cbus = NULL;
            int tries;
            for (tries = 0; tries < 200; tries++) {
                if (tachyon_bus_connect(path, &cbus) == TACHYON_SUCCESS && cbus) break;
                test_sleep_ms(10);
            }
            if (!cbus) _exit(2);
            if (tachyon_get_state(cbus) == TACHYON_STATE_FATAL_ERROR) {
                tachyon_bus_destroy(cbus);
                _exit(3);
            }
            tachyon_bus_set_polling_mode(cbus, 1);
            tachyon_flush(cbus);
            tachyon_bus_destroy(cbus);
            _exit(0);
        }
        if (pid < 0) {
            errs += CHECK(pid >= 0);
        } else {
            tachyon_bus_t *bus = NULL;
            tachyon_error_t er = tachyon_bus_listen(path, 65536, &bus);
            errs += CHECK(er == TACHYON_SUCCESS && bus != NULL);
            if (er == TACHYON_SUCCESS && bus) {
                void *slot = tachyon_acquire_tx(bus, 32);
                errs += CHECK(slot != NULL);
                if (slot) {
                    tachyon_rollback_tx(bus);
                    slot = tachyon_acquire_tx(bus, 32);
                    errs += CHECK(slot != NULL);
                    if (slot) {
                        memcpy(slot, "hello", 5);
                        errs += CHECK(tachyon_commit_tx(bus, 5, type_id) == TACHYON_SUCCESS);
                    }
                }
                errs += CHECK(tachyon_get_state(bus) != TACHYON_STATE_FATAL_ERROR);
                errs += CHECK(tachyon_bus_set_numa_node(bus, -1) != TACHYON_ERR_SYSTEM);
            }
            if (waitpid(pid, &status, 0) == pid)
                errs += CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            else
                errs += CHECK(0);
            if (bus) tachyon_bus_destroy(bus);
            unlink(path);
        }
    }

    if (errs == 0) printf("tachyon_dyn: all checks passed\n");
    return errs ? 1 : 0;
}

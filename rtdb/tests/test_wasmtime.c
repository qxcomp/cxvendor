/* wasmtime_dyn 测试：链接期只链 -ldl
 * 覆盖：wat2wasm → engine/store/module → linker+instance → add(3,4)==7
 * 本机无 libwasmtime 时 dyn_init 失败 → TEST_SKIP
 */
#include "test_common.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- 类型声明（与 ../wasmtime_dyn.c 一致） ---- */
typedef uint8_t wasm_byte_t;
typedef struct {
    size_t size;
    wasm_byte_t *data;
} wasm_byte_vec_t;
typedef wasm_byte_vec_t wasm_name_t;

typedef struct wasmtime_engine_t wasmtime_engine_t;
typedef struct wasmtime_store_t wasmtime_store_t;
typedef struct wasmtime_context_t wasmtime_context_t;
typedef struct wasmtime_module_t wasmtime_module_t;
typedef struct wasmtime_linker_t wasmtime_linker_t;
typedef struct wasmtime_trap_t wasmtime_trap_t;
typedef struct wasmtime_error_t wasmtime_error_t;

typedef struct { wasmtime_store_t *store; uint64_t index; } wasmtime_func_t;
typedef struct { wasmtime_store_t *store; uint64_t index; } wasmtime_instance_t;

typedef enum {
    WASMTIME_I32, WASMTIME_I64, WASMTIME_F32, WASMTIME_F64,
    WASMTIME_FUNC, WASMTIME_GLOBAL, WASMTIME_TABLE, WASMTIME_EXTERNREF
} wasmtime_valkind_t;

typedef struct {
    wasmtime_valkind_t kind;
    union {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
        wasmtime_func_t func;
    } of;
} wasmtime_extern_t;

typedef wasmtime_extern_t wasmtime_val_t;

/* ---- 函数声明（定义在 ../wasmtime_dyn.c） ---- */
int wasmtime_dyn_init(const char *library_path);
int wasmtime_libok(void);
void wasmtime_dyn_cleanup(void);

wasmtime_error_t *wasmtime_wat2wasm(const char *wat, size_t wat_size,
                                    wasm_byte_vec_t **out);
void wasmtime_error_delete(wasmtime_error_t *e);
void wasmtime_error_message(const wasmtime_error_t *e, wasm_name_t *msg);
wasmtime_engine_t *wasmtime_engine_new(void);
void wasmtime_engine_delete(wasmtime_engine_t *engine);
wasmtime_store_t *wasmtime_store_new(wasmtime_engine_t *engine, void *data,
                                     void (*finalizer)(void *));
void wasmtime_store_delete(wasmtime_store_t *store);
wasmtime_context_t *wasmtime_context(const wasmtime_store_t *store);
wasmtime_error_t *wasmtime_module_new(wasmtime_engine_t *engine,
                                      const wasm_byte_vec_t *wasm,
                                      wasmtime_module_t **out);
void wasmtime_module_delete(wasmtime_module_t *m);
wasmtime_linker_t *wasmtime_linker_new(const wasmtime_engine_t *engine);
void wasmtime_linker_delete(wasmtime_linker_t *l);
wasmtime_error_t *wasmtime_linker_instantiate(wasmtime_linker_t *l,
                                              const wasmtime_module_t *m,
                                              wasmtime_instance_t *instance_out,
                                              wasmtime_trap_t **trap_out);
bool wasmtime_instance_export_get(wasmtime_context_t *ctx,
                                  const wasmtime_instance_t *inst,
                                  const char *name, size_t name_len,
                                  wasmtime_extern_t *item_out);
void wasmtime_func_call(wasmtime_context_t *ctx, const wasmtime_func_t *f,
                        const wasmtime_val_t args[], size_t nargs,
                        wasmtime_val_t results[], size_t nresults,
                        wasmtime_trap_t **trap_out);
void wasmtime_linker_delete_unused_marker(void);

int main(int argc, char **argv) {
    int failures = 0;
    if (wasmtime_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        TEST_SKIP("libwasmtime 不可加载\n");
    }
    failures += CHECK(wasmtime_libok() != 0);

    /* 1. wat2wasm：加法模块 */
    const char *wat =
        "(module (func (export \"add\") (param i32 i32) (result i32)"
        "  local.get 0 local.get 1 i32.add))";
    wasm_byte_vec_t wasm;
    wasm_byte_vec_t *wasm_ptr = &wasm;
    wasmtime_error_t *err = wasmtime_wat2wasm(wat, strlen(wat), &wasm_ptr);
    failures += CHECK(err == NULL);
    if (err) {
        wasm_name_t msg;
        wasmtime_error_message(err, &msg);
        fprintf(stderr, "wat2wasm: %.*s\n", (int)msg.size, msg.data);
        wasmtime_error_delete(err);
        return 1;
    }
    printf("wat2wasm ok: %zu bytes\n", wasm.size);

    /* 2. engine + store + module */
    wasmtime_engine_t *engine = wasmtime_engine_new();
    wasmtime_store_t *store = wasmtime_store_new(engine, NULL, NULL);
    wasmtime_module_t *module = NULL;
    err = wasmtime_module_new(engine, &wasm, &module);
    failures += CHECK(err == NULL && module != NULL);
    if (err) {
        wasm_name_t msg;
        wasmtime_error_message(err, &msg);
        fprintf(stderr, "module_new: %.*s\n", (int)msg.size, msg.data);
        wasmtime_error_delete(err);
        return 1;
    }

    /* 3. linker + instantiate */
    wasmtime_linker_t *linker = wasmtime_linker_new(engine);
    wasmtime_instance_t instance;
    memset(&instance, 0, sizeof(instance));
    wasmtime_trap_t *trap = NULL;
    err = wasmtime_linker_instantiate(linker, module, &instance, &trap);
    failures += CHECK(err == NULL);
    if (err || trap) {
        fprintf(stderr, "instantiate failed (trap=%p)\n", (void *)trap);
        return 1;
    }

    /* 4. export "add" → func_call(3,4) == 7 */
    wasmtime_extern_t add_ext;
    memset(&add_ext, 0, sizeof(add_ext));
    failures += CHECK(wasmtime_instance_export_get(
        wasmtime_context(store), &instance, "add", 3, &add_ext));
    wasmtime_val_t args[2], results[1];
    memset(args, 0, sizeof(args));
    memset(results, 0, sizeof(results));
    args[0].kind = WASMTIME_I32; args[0].of.i32 = 3;
    args[1].kind = WASMTIME_I32; args[1].of.i32 = 4;
    trap = NULL;
    wasmtime_func_call(wasmtime_context(store), &add_ext.of.func,
                       args, 2, results, 1, &trap);
    failures += CHECK(trap == NULL);
    if (trap) return 1;
    failures += CHECK(results[0].kind == WASMTIME_I32);
    failures += CHECK(results[0].of.i32 == 7);
    printf("add(3,4) = %d\n", results[0].of.i32);

    /* 5. 清理 */
    wasmtime_linker_delete(linker);
    wasmtime_module_delete(module);
    wasmtime_store_delete(store);
    wasmtime_engine_delete(engine);

    wasmtime_dyn_cleanup();
    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}

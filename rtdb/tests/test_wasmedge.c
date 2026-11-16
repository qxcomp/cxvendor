/* wasmedge_dyn 测试：链接期只链 -ldl
 * 覆盖：内嵌手写 wasm 字节（add(i32,i32)->i32）→ VM load/validate/
 * instantiate → execute(3,4) == 7 → 清理
 * WasmEdge 不支持 wat，模块用手工构造的最小 wasm 二进制。
 */
#include "test_common.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---- 类型声明（与 ../wasmedge_dyn.c 一致） ---- */
typedef uint8_t WasmEdge_ValType;
typedef uint8_t WasmEdge_HostRegistration;
typedef uint32_t WasmEdge_Result;

typedef struct {
    const char *Buf;
    uint32_t Length;
} WasmEdge_String;

typedef struct {
    WasmEdge_ValType Type;
    union {
        int32_t I32;
        int64_t I64;
        float F32;
        double F64;
        const void *Ref;
        struct { uint64_t H; uint64_t L; } V128;
    } Value;
} WasmEdge_Value;

typedef struct WasmEdge_VMContext WasmEdge_VMContext;
typedef struct WasmEdge_ConfigureContext WasmEdge_ConfigureContext;
typedef struct WasmEdge_FunctionTypeContext WasmEdge_FunctionTypeContext;

#define WasmEdge_Result_Success   ((WasmEdge_Result)0)
#define WasmEdge_Result_Fail      ((WasmEdge_Result)2)

/* ---- 函数声明（定义在 ../wasmedge_dyn.c） ---- */
int wasmedge_dyn_init(const char *library_path);
int wasmedge_dyn_libok(void);
void wasmedge_dyn_cleanup(void);

WasmEdge_VMContext *WasmEdge_VMCreate(const WasmEdge_ConfigureContext *conf,
                                      void *exec);
void WasmEdge_VMDelete(WasmEdge_VMContext *vm);
WasmEdge_Result WasmEdge_VMLoadWasmFromBuffer(WasmEdge_VMContext *vm,
                                              const uint8_t *buf,
                                              uint32_t buf_len);
WasmEdge_Result WasmEdge_VMValidate(WasmEdge_VMContext *vm);
WasmEdge_Result WasmEdge_VMInstantiate(WasmEdge_VMContext *vm);
WasmEdge_Result WasmEdge_VMExecute(WasmEdge_VMContext *vm,
                                   const WasmEdge_String name,
                                   const WasmEdge_Value *params,
                                   const uint32_t plen,
                                   WasmEdge_Value *returns,
                                   const uint32_t rlen);
WasmEdge_Result WasmEdge_VMCleanup(WasmEdge_VMContext *vm);
WasmEdge_ConfigureContext *WasmEdge_ConfigureCreate(void);
void WasmEdge_ConfigureDelete(WasmEdge_ConfigureContext *c);
WasmEdge_String WasmEdge_StringCreateByCString(const char *s);
void WasmEdge_StringDelete(WasmEdge_String s);
WasmEdge_Value WasmEdge_ValueGenI32(const int32_t v);
int32_t WasmEdge_ValueGetI32(const WasmEdge_Value v);
void WasmEdge_LogSetErrorLevel(void);
const char *WasmEdge_ResultGetMessage(const WasmEdge_Result r);
int wasmedge_dyn_result_ok(WasmEdge_Result r);

/* 最小 wasm 模块：type(i32,i32)->i32, func, export "add", code */
static const uint8_t add_wasm[] = {
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,              /* header */
    0x01, 0x07, 0x01, 0x60, 0x02, 0x7F, 0x7F, 0x01, 0x7F,        /* type */
    0x03, 0x02, 0x01, 0x00,                                      /* func */
    0x07, 0x07, 0x01, 0x03, 'a', 'd', 'd', 0x00, 0x00,           /* export */
    0x0A, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01,        /* code */
    0x6A, 0x0B
};

int main(int argc, char **argv) {
    int failures = 0;
    if (wasmedge_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        TEST_SKIP("libwasmedge 不可加载\n");
    }
    failures += CHECK(wasmedge_dyn_libok() != 0);
    WasmEdge_LogSetErrorLevel();

    WasmEdge_ConfigureContext *conf = WasmEdge_ConfigureCreate();
    failures += CHECK(conf != NULL);
    WasmEdge_VMContext *vm = WasmEdge_VMCreate(conf, NULL);
    failures += CHECK(vm != NULL);
    if (!vm) return 1;

    WasmEdge_Result r = WasmEdge_VMLoadWasmFromBuffer(
        vm, add_wasm, sizeof(add_wasm));
    failures += CHECK(r == WasmEdge_Result_Success);
    if (r != WasmEdge_Result_Success) {
        fprintf(stderr, "load: %s\n", WasmEdge_ResultGetMessage(r));
        return 1;
    }
    failures += CHECK(WasmEdge_VMValidate(vm) == WasmEdge_Result_Success);
    failures += CHECK(WasmEdge_VMInstantiate(vm) == WasmEdge_Result_Success);

    WasmEdge_String fn = WasmEdge_StringCreateByCString("add");
    WasmEdge_Value params[2], returns[1];
    memset(params, 0, sizeof(params));
    memset(returns, 0, sizeof(returns));
    params[0] = WasmEdge_ValueGenI32(3);
    params[1] = WasmEdge_ValueGenI32(4);
    r = WasmEdge_VMExecute(vm, fn, params, 2, returns, 1);
    failures += CHECK(r == WasmEdge_Result_Success);
    if (r != WasmEdge_Result_Success) {
        fprintf(stderr, "execute: %s\n", WasmEdge_ResultGetMessage(r));
        return 1;
    }
    failures += CHECK(WasmEdge_ValueGetI32(returns[0]) == 7);
    printf("add(3,4) = %d\n", WasmEdge_ValueGetI32(returns[0]));

    failures += CHECK(wasmedge_dyn_result_ok(r));

    WasmEdge_StringDelete(fn);
    WasmEdge_VMCleanup(vm);
    WasmEdge_VMDelete(vm);
    WasmEdge_ConfigureDelete(conf);

    wasmedge_dyn_cleanup();
    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}

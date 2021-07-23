#ifndef MINNET_JSUTILS_H
#define MINNET_JSUTILS_H

#include <quickjs.h>
#include <list.h>

typedef struct JSThreadState {
  struct list_head os_rw_handlers;
  struct list_head os_signal_handlers;
  struct list_head os_timers;
  struct list_head port_list;
  int eval_script_recurse;
  void *recv_pipe, *send_pipe;
} JSThreadState;

static void
js_console_log(JSContext* ctx, JSValue* console, JSValue* console_log) {
  JSValue global = JS_GetGlobalObject(ctx);
  *console = JS_GetPropertyStr(ctx, global, "console");
  *console_log = JS_GetPropertyStr(ctx, *console, "log");
  JS_FreeValue(ctx, global);
}

static JSValue
js_function_bound(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue* func_data) {
  JSValue args[argc + magic];
  size_t i, j;
  for(i = 0; i < magic; i++) args[i] = func_data[i + 1];
  for(j = 0; j < argc; j++) args[i++] = argv[j];

  return JS_Call(ctx, func_data[0], this_val, i, args);
}

static JSValue
js_function_bind(JSContext* ctx, JSValueConst func, int argc, JSValueConst argv[]) {
  JSValue data[argc + 1];
  size_t i;
  data[0] = JS_DupValue(ctx, func);
  for(i = 0; i < argc; i++) data[i + 1] = JS_DupValue(ctx, argv[i]);
  return JS_NewCFunctionData(ctx, js_function_bound, 0, argc, argc + 1, data);
}

static inline JSValue
js_function_bind_1(JSContext* ctx, JSValueConst func, JSValueConst arg) {
  return js_function_bind(ctx, func, 1, &arg);
}

typedef union pointer {
  void* p;
  struct {
    int32_t lo32, hi32;
  };
  uint64_t u64;
  int64_t s64;
  uint32_t u32[2];
  int32_t s32[2];
  uint16_t u16[4];
  int16_t s16[4];
  uint8_t u8[8];
  int8_t s8[8];
} Pointer;

static inline Pointer
ptr(const void* ptr) {
  Pointer r = {0};
  r.p = ptr;
  return r;
}

static inline void*
ptr32(uint32_t lo, uint32_t hi) {
  Pointer r = {0};
  r.u32[0] = lo;
  r.u32[1] = hi;
  return r.p;
}

#endif /* MINNET_JS_UTILS_H */
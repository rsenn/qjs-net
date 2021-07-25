#ifndef MINNET_JSUTILS_H
#define MINNET_JSUTILS_H

#include <quickjs.h>
#include <cutils.h>
#include <list.h>

typedef struct JSThreadState {
  struct list_head os_rw_handlers;
  struct list_head os_signal_handlers;
  struct list_head os_timers;
  struct list_head port_list;
  int eval_script_recurse;
  void *recv_pipe, *send_pipe;
} JSThreadState;

JSValue vector2array(JSContext*, int argc, JSValue argv[]);
void js_console_log(JSContext*, JSValue* console, JSValue* console_log);
JSValue js_function_bound(JSContext*, JSValue this_val, int argc, JSValue argv[], int magic, JSValue* func_data);
JSValue js_function_bind(JSContext*, JSValue func, int argc, JSValue argv[]);
JSValue js_function_bind_1(JSContext*, JSValue func, JSValue arg);
BOOL js_is_iterator(JSContext*, JSValue obj);
JSValue js_iterator_next(JSContext*, JSValue obj, JSValue* next, BOOL* done_p);

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
  r.p = (void*)ptr;
  return r;
}

static inline JSValue
ptr2value(JSContext* ctx, const void* ptr) {
  char buf[128];
  size_t len;
  len = snprintf(buf, sizeof(buf), "0x%llx", (long long)ptr);
  return JS_NewStringLen(ctx, buf, len);
}

static inline void*
value2ptr(JSContext* ctx, JSValueConst value) {
  Pointer r = {ptr};

  if(JS_ToIndex(ctx, &r.u64, value)) {
    const char* str = JS_ToCString(ctx, value);
    BOOL hex = str[0] == '0' && str[1] == 'x';

    r.u64 = strtoull(hex ? str + 2 : str, 0, hex ? 16 : 10);
  }
  return r.p;
}

static inline void*
ptr32(uint32_t lo, uint32_t hi) {
  Pointer r = {0};
  r.u32[0] = lo;
  r.u32[1] = hi;
  return r.p;
}

static inline void*
values32ptr(JSContext* ctx, JSValueConst values[2]) {
  uint32_t lo, hi;
  JS_ToUint32(ctx, &lo, values[0]);
  JS_ToUint32(ctx, &hi, values[1]);
  return ptr32(lo, hi);
}

static inline JSValue
ptr32value(JSContext* ctx, const void* ptr, int index) {
  Pointer r = {(void*)ptr};
  return JS_NewUint32(ctx, r.u32[index]);
}

#endif /* MINNET_JS_UTILS_H */

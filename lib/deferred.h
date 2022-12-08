#ifndef QUICKJS_NET_LIB_DEFERRED_H
#define QUICKJS_NET_LIB_DEFERRED_H

#include <quickjs.h>
#include <cutils.h>
#include <stdarg.h>

struct deferred;
typedef void* ptr_t;
typedef void FinalizerFunction(struct deferred*);
typedef void js_ctx_function_t(JSContext*, JSValueConst);
typedef void js_rt_function_t(JSRuntime*, JSValueConst);

typedef union dword {
  struct {
    ptr_t lo, hi;
  };
  ptr_t word[2];
  JSValueConst js;
} DoubleWord;

typedef DoubleWord DeferredFunction(ptr_t, ptr_t, ptr_t, ptr_t, ptr_t, ptr_t, ptr_t, ptr_t);

typedef struct deferred {
  int ref_count, num_calls;
  BOOL only_once;
  DeferredFunction* func;
  // FinalizerFunction* finalize;
  ptr_t args[8], opaque;
  DoubleWord retval;
  struct deferred* next;
} Deferred;

void deferred_clear(Deferred*);
void deferred_free(Deferred*);
Deferred* deferred_new(ptr_t, int argc, ptr_t argv[], JSContext* ctx);
Deferred* deferred_newjs(js_ctx_function_t, JSValueConst v, JSContext* ctx);
Deferred* deferred_dupjs(js_ctx_function_t, JSValueConst value, JSContext* ctx);
Deferred* deferred_newjs_rt(js_rt_function_t, JSValueConst value, JSContext* ctx);
void deferred_init(Deferred*, ptr_t fn, int argc, ptr_t argv[]);
DoubleWord deferred_call(Deferred*);
JSValue deferred_js(Deferred*, JSContext* ctx);

static inline Deferred*
deferred_new_va(ptr_t fn, JSContext* ctx, ...) {
  va_list a;
  int argc = 0;
  ptr_t args[8] = {0}, arg;
  va_start(a, ctx);

  while((arg = va_arg(a, void*))) { args[argc++] = arg; }

  va_end(a);

  return deferred_new(fn, argc, args, ctx);
}

static inline Deferred*
deferred_dup(Deferred* def) {
  ++def->ref_count;
  return def;
}

static inline JSValue
deferred_getjs(Deferred* def) {
  return *(JSValue*)&def->args[1];
}

static inline Deferred*
deferred_new1(ptr_t fn, ptr_t arg1, JSContext* ctx) {
  return deferred_new(fn, 1, &arg1, ctx);
}

static inline Deferred*
deferred_new2(ptr_t fn, ptr_t arg1, ptr_t arg2, JSContext* ctx) {
  ptr_t args[] = {
      arg1,
      arg2,
  };
  return deferred_new(fn, 2, args, ctx);
}

static inline Deferred*
deferred_new3(ptr_t fn, ptr_t arg1, ptr_t arg2, ptr_t arg3, JSContext* ctx) {
  ptr_t args[] = {
      arg1,
      arg2,
      arg3,
  };
  return deferred_new(fn, 3, args, ctx);
}
#endif /* QUICKJS_NET_LIB_DEFERRED_H */

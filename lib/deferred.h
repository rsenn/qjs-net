#ifndef QJSNET_LIB_DEFERRED_H
#define QJSNET_LIB_DEFERRED_H

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

/*typedef struct jscall_args {
  JSContext* ctx;
  JSValueConst func_obj;
  JSValueConst this_val;
  int argc;
  JSValueConst* argv;
  int magic;
  ptr_t ptr;
} JSCallArgs;

typedef struct jsfn_args {
  JSContext* ctx;
  JSValueConst this_val;
  int argc;
  JSValueConst* argv;
  int magic;
  ptr_t ptr;
} JSFunctionArgs;*/

typedef DoubleWord DeferredFunction(ptr_t, ptr_t, ptr_t, ptr_t, ptr_t, ptr_t, ptr_t, ptr_t);
// typedef JSValue DeferredJSFunction(JSContext*, JSValueConst, JSValueConst, int, JSValueConst[]);

typedef struct deferred {
  int ref_count, num_calls;
  BOOL only_once, js;
  union {
    DeferredFunction* func;
    // DeferredJSFunction* jscall;
  };
  // FinalizerFunction* finalize;
  int argc;
  // union {
  ptr_t argv[8];
  // JSValueConst jsargs[4];
  //};
  ptr_t opaque;
  DoubleWord retval;
  struct deferred* next;
} Deferred;

void deferred_clear(Deferred*);
void deferred_free(Deferred*);
Deferred* deferred_new(ptr_t, int argc, ptr_t argv[]);
Deferred* deferred_newjs(js_ctx_function_t, JSValueConst v, JSContext* ctx);
Deferred* deferred_dupjs(js_ctx_function_t, JSValueConst value, JSContext* ctx);
Deferred* deferred_newjs_rt(js_rt_function_t, JSValueConst value, JSContext* ctx);
void deferred_init(Deferred*, ptr_t fn, int argc, ptr_t argv[]);
DoubleWord deferred_call(Deferred*);
JSValue deferred_tojs(Deferred*, JSContext* ctx);

static inline Deferred*
deferred_new_va(ptr_t fn, ...) {
  va_list a;
  int argc = 0;
  ptr_t args[8] = {0}, arg;
  va_start(a, fn);

  while((arg = va_arg(a, void*))) { args[argc++] = arg; }

  va_end(a);

  return deferred_new(fn, argc, args);
}

static inline Deferred*
deferred_dup(Deferred* def) {
  ++def->ref_count;
  return def;
}

static inline JSContext*
deferred_getctx(Deferred* def) {
  return (JSContext*)def->argv[0];
}

static inline JSValue
deferred_getjs(Deferred* def) {
  return *(JSValue*)&def->argv[1];
}

static inline Deferred*
deferred_new1(ptr_t fn, ptr_t arg1) {
  return deferred_new(fn, 1, &arg1);
}

static inline Deferred*
deferred_new2(ptr_t fn, ptr_t arg1, ptr_t arg2) {
  ptr_t args[] = {
      arg1,
      arg2,
  };
  return deferred_new(fn, 2, args);
}

static inline Deferred*
deferred_new3(ptr_t fn, ptr_t arg1, ptr_t arg2, ptr_t arg3) {
  ptr_t args[] = {
      arg1,
      arg2,
      arg3,
  };
  return deferred_new(fn, 3, args);
}
#endif /* QJSNET_LIB_DEFERRED_H */

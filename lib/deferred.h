#ifndef QUICKJS_NET_LIB_DEFERRED_H
#define QUICKJS_NET_LIB_DEFERRED_H

#include <quickjs.h>
#include <cutils.h>

struct deferred;
typedef void* ptr_t;
typedef ptr_t deferred_function_t(ptr_t, ptr_t, ptr_t, ptr_t, ptr_t, ptr_t, ptr_t, ptr_t);
typedef void free_function_t(struct deferred*);
typedef void js_ctx_function_t(JSContext*, JSValueConst);
typedef void js_rt_function_t(JSRuntime*, JSValueConst);

typedef struct deferred {
  int ref_count, num_calls;
  BOOL only_once;
  deferred_function_t* func;
  free_function_t* finalize;
  ptr_t args[8], retval, opaque;
} Deferred;

void deferred_clear(Deferred*);
void deferred_free(Deferred*);
Deferred* deferred_new(ptr_t, int argc, ptr_t argv[], JSContext* ctx);
Deferred* deferred_newjs(js_ctx_function_t, JSValueConst v, JSContext* ctx);
Deferred* deferred_dupjs(js_ctx_function_t, JSValueConst value, JSContext* ctx);
Deferred* deferred_newjs_rt(js_rt_function_t, JSValueConst value, JSContext* ctx);
void deferred_init(Deferred*, ptr_t fn, int argc, ptr_t argv[]);
ptr_t deferred_call(Deferred*);
JSValue deferred_js(Deferred*, JSContext* ctx);

static inline Deferred*
deferred_dup(Deferred* def) {
  ++def->ref_count;
  return def;
}

static inline JSValue
deferred_getjs(Deferred* def) {
  return *(JSValue*)&def->args[1];
}

#endif /* QUICKJS_NET_LIB_DEFERRED_H */

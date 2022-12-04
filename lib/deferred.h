#ifndef QUICKJS_NET_LIB_DEFERRED_H
#define QUICKJS_NET_LIB_DEFERRED_H

#include <quickjs.h>
#include <cutils.h>

typedef void* ptr_t;
typedef ptr_t deferred_function_t(ptr_t, ptr_t, ptr_t, ptr_t, ptr_t, ptr_t, ptr_t, ptr_t);

typedef struct deferred {
  int ref_count, num_calls;
  BOOL only_once;
  deferred_function_t* func;
  ptr_t args[8], retval, opaque;
} Deferred;

void      deferred_clear(Deferred* def);
void      deferred_free(Deferred* def);
Deferred* deferred_new(ptr_t fn, int argc, ptr_t argv[], JSContext* ctx);
Deferred* deferred_newjs(ptr_t fn, JSValueConst value, JSContext* ctx);
void      deferred_init(Deferred* def, ptr_t fn, int argc, ptr_t argv[]);
ptr_t     deferred_call(Deferred* def);
JSValue   deferred_js(Deferred* def, JSContext* ctx);

static inline Deferred*
deferred_dup(Deferred* def) {
  ++def->ref_count;
  return def;
}

#endif /* QUICKJS_NET_LIB_DEFERRED_H */

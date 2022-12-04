#ifndef QUICKJS_NET_LIB_REF_H
#define QUICKJS_NET_LIB_REF_H

#include "deferred.h"

typedef struct ref {
  int ref_count;
  Deferred dup, free;
} Ref;

void ref_clear(Ref* r);
void ref_free(Ref* r);
Ref* ref_new(ptr_t dup, ptr_t free, int argc, ptr_t argv[], JSContext* ctx);
Ref* ref_new2(ptr_t dup, ptr_t free, ptr_t a1, ptr_t a2, JSContext* ctx);
Ref* ref_new3(ptr_t dup, ptr_t free, ptr_t a1, ptr_t a2, ptr_t a3, JSContext* ctx);
Ref* ref_newjs(ptr_t dup, ptr_t free, JSValueConst arg, JSContext* ctx);

static inline Ref*
ref_dup(Ref* r) {
  ++r->ref_count;
  return r;
}

#endif /* QUICKJS_NET_LIB_REF_H */

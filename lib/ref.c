#include <assert.h>
#include "ref.h"

void
ref_clear(Ref* r) {
  deferred_clear(&r->dup);
  deferred_clear(&r->free);
}

void
ref_free(Ref* r) {
  if(--r->ref_count == 0) {
    JSContext* ctx = r->dup.argv[0];
    ref_clear(r);
    js_free(ctx, r);
  }
}

Ref*
ref_new(ptr_t dup, ptr_t free, int argc, ptr_t argv[], JSContext* ctx) {
  Ref* r;

  if(!(r = js_malloc(ctx, sizeof(Ref))))
    return 0;

  r->ref_count = 1;

  deferred_init(&r->dup, dup, argc, argv);
  deferred_init(&r->free, free, argc, argv);
  return r;
}

Ref*
ref_new2(ptr_t dup, ptr_t free, ptr_t a1, ptr_t a2, JSContext* ctx) {
  Ref* r;
  ptr_t args[] = {
      a1,
      a2,
  };

  if(!(r = js_malloc(ctx, sizeof(Ref))))
    return 0;

  r->ref_count = 1;

  deferred_init(&r->dup, dup, 2, args);
  deferred_init(&r->free, free, 2, args);
  return r;
}

Ref*
ref_new3(ptr_t dup, ptr_t free, ptr_t a1, ptr_t a2, ptr_t a3, JSContext* ctx) {
  Ref* r;
  ptr_t args[] = {
      a1,
      a2,
      a3,
  };

  if(!(r = js_malloc(ctx, sizeof(Ref))))
    return 0;

  r->ref_count = 1;

  deferred_init(&r->dup, dup, 3, args);
  deferred_init(&r->free, free, 3, args);
  return r;
}

Ref*
ref_newjs(ptr_t dup, ptr_t free, JSValueConst arg, JSContext* ctx) {
  return ref_new3(dup, free, ctx, ((ptr_t*)&arg)[0], ((ptr_t*)&arg)[1], ctx);
}

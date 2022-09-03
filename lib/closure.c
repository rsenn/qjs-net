#include "closure.h"
#include "context.h"

MinnetClosure*
closure_new(JSContext* ctx) {
  MinnetClosure* closure;

  if((closure = js_mallocz(ctx, sizeof(MinnetClosure)))) {
    closure->ref_count = 1;
    closure->ctx = ctx;
  }

  return closure;
}

MinnetClosure*
closure_dup(MinnetClosure* c) {
  ++c->ref_count;
  return c;
}

void
closure_free(void* ptr) {
  MinnetClosure* closure = ptr;

  if(--closure->ref_count == 0) {
    JSContext* ctx = closure->ctx;
    // printf("%s server=%p\n", __func__, closure->server);

    if(closure->free_func)
      closure->free_func(closure->pointer);

    js_free(ctx, closure);
  }
}

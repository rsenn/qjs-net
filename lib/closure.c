#include "closure.h"
#include "context.h"

union closure*
closure_new(JSContext* ctx) {
  union closure* closure;

  if((closure = js_mallocz(ctx, sizeof(union closure)))) {
    closure->ref_count = 1;
    closure->ctx = ctx;
  }

  return closure;
}

union closure*
closure_dup(union closure* c) {
  ++c->ref_count;
  return c;
}

void
closure_free(void* ptr) {
  union closure* closure = ptr;

  if(--closure->ref_count == 0) {
    JSContext* ctx = closure->ctx;
    // printf("%s() pointer=%p\n", __func__, closure->pointer);

    if(closure->free_func) {
      closure->free_func(closure->pointer, JS_GetRuntime(ctx));
      closure->pointer = NULL;
    }

    js_free(ctx, closure);
  }
}

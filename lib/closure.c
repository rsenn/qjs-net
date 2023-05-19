#include <assert.h>
#include "closure.h"
#include "context.h"

union closure*
closure_new(JSContext* ctx) {
  union closure* closure;

  if((closure = js_mallocz(ctx, sizeof(union closure)))) {
    closure->ref_count = 1;
    closure->rt = JS_GetRuntime(ctx);
  }

  return closure;
}

void
closure_free_object(void* ptr, JSRuntime* rt) {
  JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_OBJECT, ptr));
}

union closure*
closure_object(JSContext* ctx, JSValueConst val) {
  union closure* ret;

  assert(JS_IsObject(val));
  JS_DupValue(ctx, val);

  if((ret = closure_new(ctx))) {
    ret->pointer = JS_VALUE_GET_OBJ(val);
    ret->free_func = &closure_free_object;
  }

  return ret;
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
    // printf("%s() pointer=%p\n", __func__, closure->pointer);

    if(closure->free_func) {
      closure->free_func(closure->pointer, closure->rt);
      closure->pointer = NULL;
    }

    js_free_rt(closure->rt, closure);
  }
}

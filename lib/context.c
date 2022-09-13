#include "context.h"

JSValue
context_exception(struct context* context, JSValue retval) {
  if(JS_IsException(retval)) {
    context->exception = TRUE;
    JSValue exception = JS_GetException(context->js);
    JSValue stack = JS_GetPropertyStr(context->js, exception, "stack");
    const char* err = JS_ToCString(context->js, exception);
    const char* stk = JS_ToCString(context->js, stack);
    printf("Got exception: %s\n%s\n", err, stk);
    JS_FreeCString(context->js, err);
    JS_FreeCString(context->js, stk);
    JS_FreeValue(context->js, stack);
    JS_Throw(context->js, exception);
  }

  return retval;
}

void
context_clear(struct context* context) {
  JSContext* ctx = context->js;

  lws_set_log_level(0, 0);

  lws_context_destroy(context->lws);
  // lws_set_log_level(((unsigned)minnet_log_level & ((1u << LLL_COUNT) - 1)), minnet_log_callback);

  JS_FreeValue(ctx, context->crt);
  JS_FreeValue(ctx, context->key);
  JS_FreeValue(ctx, context->ca);

  JS_FreeValue(ctx, context->error);
}

/**
 * @file context.c
 */
#include <assert.h>
#include <libwebsockets.h>
#include "context.h"
#include "utils.h"

THREAD_LOCAL struct list_head context_list = {0, 0};

JSValue
context_exception(struct context* context, JSValue value) {
  if(JS_IsException(value)) {
    JSValue stack;
    const char *err, *stk;

    context->exception = TRUE;
    context->error = JS_GetException(context->js);

    stack = JS_GetPropertyStr(context->js, context->error, "stack");
    err = JS_ToCString(context->js, context->error);
    stk = JS_ToCString(context->js, stack);
    // printf("Got exception: %s\n%s\n", err, stk);

    js_error_print(context->js, context->error);

    JS_FreeCString(context->js, err);
    JS_FreeCString(context->js, stk);
    JS_FreeValue(context->js, stack);

    JS_Throw(context->js, context->error);
    js_async_reject(context->js, &context->promise, context->error);
  }

  return value;
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

void
context_add(struct context* context) {
  if(context_list.next == 0 && context_list.prev == 0)
    init_list_head(&context_list);

  list_add(&context->link, &context_list);
}

void
context_delete(struct context* context) {
  if(context->link.next || context->link.prev)
    list_del(&context->link);
}

/*struct context*
context_for_fd(int fd, struct lws** p_wsi) {
  struct list_head* el;

  if(context_list.next == 0 && context_list.prev == 0)
    init_list_head(&context_list);

  list_for_each(el, &context_list) {
    struct context* context = list_entry(el, struct context, link);
    struct lws* wsi;

    if((wsi = wsi_from_fd(context->lws, fd))) {
      if(p_wsi)
        *p_wsi = wsi;
      return context;
    }
  }

  return 0;
}*/

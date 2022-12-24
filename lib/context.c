#include <libwebsockets.h>
#include "context.h"
#include "utils.h"

THREAD_LOCAL struct list_head context_list = {0, 0};

JSValue
context_exception(struct context* context, JSValue retval) {
  if(JS_IsException(retval)) {
    context->exception = TRUE;
    context->error = JS_GetException(context->js);

    JSValue stack = JS_GetPropertyStr(context->js, context->error, "stack");
    const char* err = JS_ToCString(context->js, context->error);
    const char* stk = JS_ToCString(context->js, stack);
    printf("Got exception: %s\n%s\n", err, stk);
    JS_FreeCString(context->js, err);
    JS_FreeCString(context->js, stk);
    JS_FreeValue(context->js, stack);
    JS_Throw(context->js, context->error);
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

void
context_add(struct context* context) {

  if(context_list.next == 0 && context_list.prev == 0)
    init_list_head(&context_list);

  list_add(&context->link, &context_list);
}

void
context_delete(struct context* context) {
  list_del(&context->link);
}

struct context*
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
}

struct context*
context_for_wsi(int, struct lws* wsi) {
  return lws_context_user(lws_get_context(wsi));
}

#include "minnet-callback.h"

int
fd_handler(struct lws* wsi, MinnetCallback* cb, struct lws_pollargs args) {
  JSValue argv[3] = {JS_NewInt32(cb->ctx, args.fd)};

  minnet_handlers(cb->ctx, wsi, args, &argv[1]);
  minnet_emit(cb, 3, argv);

  JS_FreeValue(cb->ctx, argv[0]);
  JS_FreeValue(cb->ctx, argv[1]);
  JS_FreeValue(cb->ctx, argv[2]);
  return 0;
}

int
fd_callback(struct lws* wsi, enum lws_callback_reasons reason, MinnetCallback* cb, struct lws_pollargs* args) {

  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: return 0;

    case LWS_CALLBACK_ADD_POLL_FD: {

      if(cb->ctx) {
        fd_handler(wsi, cb, *args);
      }
      return 0;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {

      if(cb->ctx) {
        fd_handler(wsi, cb, *args);
      }
      return 0;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      if(cb->ctx) {
        if(args->events != args->prev_events) {
          fd_handler(wsi, cb, *args);
        }
      }
      return 0;
    }

    default: {
      return -1;
    }
  }
}


JSValue
minnet_emit_this(const struct ws_callback* cb, JSValueConst this_obj, int argc, JSValue* argv) {
  JSValue ret = JS_UNDEFINED;

  if(cb->ctx) {
    /*size_t len;
    const char* str  = JS_ToCStringLen(cb->ctx, &len, cb->func_obj);
    // printf("emit %s [%d] \"%.*s\"\n", cb->name, argc, (int)((const char*)memchr(str, '{', len) - str), str);
    JS_FreeCString(cb->ctx, str);*/

    ret = JS_Call(cb->ctx, cb->func_obj, this_obj, argc, argv);
  }

  if(JS_IsException(ret)) {
    JSValue exception = JS_GetException(cb->ctx);
    js_error_print(cb->ctx, exception);
    ret = JS_Throw(cb->ctx, exception);
  }
  /*if(JS_IsException(ret))
    minnet_exception = TRUE; */

  return ret;
}

JSValue
minnet_emit(const struct ws_callback* cb, int argc, JSValue* argv) {
  return minnet_emit_this(cb, cb->this_obj /* ? *cb->this_obj : JS_NULL*/, argc, argv);
}

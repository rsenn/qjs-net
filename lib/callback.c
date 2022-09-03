#include "callback.h"
#include "jsutils.h"
#include "utils.h"
#include "opaque.h"
#include <assert.h>

int
fd_handler(struct lws* wsi, JSCallback* cb, struct lws_pollargs args) {
  JSValue argv[3] = {JS_NewInt32(cb->ctx, args.fd)};

  callback_handlers(cb->ctx, wsi, args, &argv[1]);
  callback_emit(cb, 3, argv);

  JS_FreeValue(cb->ctx, argv[0]);
  JS_FreeValue(cb->ctx, argv[1]);
  JS_FreeValue(cb->ctx, argv[2]);
  return 0;
}

int
fd_callback(struct lws* wsi, enum lws_callback_reasons reason, JSCallback* cb, struct lws_pollargs* args) {

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
callback_emit_this(const JSCallback* cb, JSValueConst this_obj, int argc, JSValue* argv) {
  JSValue ret = JS_UNDEFINED;

  if(cb->ctx)
    ret = JS_Call(cb->ctx, cb->func_obj, this_obj, argc, argv);

  if(JS_IsException(ret)) {
    JSValue exception = JS_GetException(cb->ctx);
    js_error_print(cb->ctx, exception);
    ret = JS_Throw(cb->ctx, exception);
  }
  return ret;
}

JSValue
callback_emit(const JSCallback* cb, int argc, JSValue* argv) {
  return callback_emit_this(cb, cb->this_obj /* ? *cb->this_obj : JS_NULL*/, argc, argv);
}

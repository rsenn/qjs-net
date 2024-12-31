#define _GNU_SOURCE
#include "minnet-request.h"
#include "minnet-response.h"
#include "minnet-client.h"
#include "minnet.h"
#include "buffer.h"
#include "closure.h"
#include "js-utils.h"
#include <strings.h>
#include <quickjs.h>

enum {
  ON_HTTP = 0,
  ON_ERROR,
  ON_CLOSE,
  ON_FD,
};

static JSValue
fetch_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  union closure* closure = opaque;
  MinnetClient* client = closure->pointer;

#ifdef DEBUG_OUTPUT
  lwsl_user("DEBUG                    %-22s magic=%s client=%p", __func__, magic == ON_HTTP ? "ON_HTTP" : magic == ON_ERROR ? "ON_ERROR" : magic == ON_CLOSE ? "ON_CLOSE" : "ON_FD", client);
#endif

  switch(magic) {
    case ON_HTTP: {
      if(js_async_pending(&client->promise))
        js_async_resolve(ctx, &client->promise, argv[1]);

      return JS_NewInt32(ctx, 0);
    }

    case ON_CLOSE:
    case ON_ERROR: {
      const char* str = JS_ToCString(ctx, argv[1]);
      JSValue err = js_error_new(ctx, "%s: %s", magic == ON_CLOSE ? "onClose" : "onError", str);
      JS_FreeCString(ctx, str);

      if(js_async_pending(&client->promise))
        js_async_reject(ctx, &client->promise, err);

      JS_FreeValue(ctx, err);
      break;
    }

    case ON_FD: {
      JSValue os, tmp, set_write, set_read, args[2] = {argv[0], JS_NULL};

      os = js_global_get(ctx, "os");

      if(!JS_IsObject(os))
        return JS_ThrowTypeError(ctx, "globalThis.os must be imported module");

      set_read = JS_GetPropertyStr(ctx, os, "setReadHandler");
      set_write = JS_GetPropertyStr(ctx, os, "setWriteHandler");
      args[1] = argv[1];
      tmp = JS_Call(ctx, set_read, os, 2, args);

      JS_FreeValue(ctx, tmp);

      args[1] = argv[2];
      tmp = JS_Call(ctx, set_write, os, 2, args);

      JS_FreeValue(ctx, tmp);
      JS_FreeValue(ctx, os);
      JS_FreeValue(ctx, set_write);
      JS_FreeValue(ctx, set_read);
      break;
    }
  }

  return JS_UNDEFINED;
}

JSValue
minnet_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret, handlers[4], args[2];
  union closure* cc;
  BOOL block = TRUE;

  if(argc >= 2 && !JS_IsObject(argv[1]))
    return JS_ThrowTypeError(ctx, "argument 2 must be an object");

  if(!(cc = closure_new(ctx)))
    return JS_EXCEPTION;

  args[0] = argv[0];
  args[1] = argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_NewObject(ctx);

  if(argc > 1 && js_has_propertystr(ctx, argv[1], "block"))
    block = js_get_propertystr_bool(ctx, argv[1], "block");

  handlers[0] = js_function_cclosure(ctx, &fetch_handler, 2, ON_HTTP, closure_dup(cc), closure_free);
  handlers[1] = js_function_cclosure(ctx, &fetch_handler, 2, ON_ERROR, closure_dup(cc), closure_free);
  handlers[2] = js_function_cclosure(ctx, &fetch_handler, 2, ON_CLOSE, closure_dup(cc), closure_free);
  // handlers[3] = js_function_cclosure(ctx, &fetch_handler, 3, ON_FD, closure_dup(cc), closure_free);

  JS_SetPropertyStr(ctx, args[1], "onResponse", handlers[0]);
  JS_SetPropertyStr(ctx, args[1], "onError", handlers[1]);
  JS_SetPropertyStr(ctx, args[1], "onClose", handlers[2]);
  // JS_SetPropertyStr(ctx, args[1], "onFd", handlers[3]);

  if(!js_has_propertystr(ctx, args[1], "block"))
    JS_SetPropertyStr(ctx, args[1], "block", JS_NewBool(ctx, block));

  ret = minnet_client_closure(ctx, this_val, 2, args, RETURN_RESPONSE, cc);

  JS_FreeValue(ctx, args[1]);

#ifdef DEBUG_OUTPUT
  lwsl_user("DEBUG                    %-22s url=%s client=%p", __func__, JS_ToCString(ctx, args[0]), cc->pointer);
#endif

  cc->pointer = minnet_client_dup(cc->pointer);

  return ret;
}

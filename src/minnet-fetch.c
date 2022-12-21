#define _GNU_SOURCE
#include "minnet-request.h"
#include "minnet-response.h"
#include "minnet-client.h"
#include "minnet.h"
#include "buffer.h"
#include "closure.h"
#include "jsutils.h"
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

  DEBUG("%s magic=%s client=%p\n", __func__, magic == ON_HTTP ? "ON_HTTP" : magic == ON_ERROR ? "ON_ERROR" : "ON_FD", client);

  switch(magic) {
    case ON_HTTP: {
      if(js_promise_pending(&client->promise))
        js_promise_resolve(ctx, &client->promise, argv[1]);

      return JS_NewInt32(ctx, 0);
      break;
    }

    case ON_CLOSE:
    case ON_ERROR: {
      const char* str = JS_ToCString(ctx, argv[1]);
      JSValue err = js_error_new(ctx, "%s: %s", magic == ON_CLOSE ? "onClose" : "onError", str);
      JS_FreeCString(ctx, str);

      //  JS_SetPropertyStr(ctx, err, "message", JS_DupValue(ctx, argv[1]));
      if(js_promise_pending(&client->promise))
        js_promise_reject(ctx, &client->promise, err);
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
  // MinnetFetch* fc;

  if(argc >= 2 && !JS_IsObject(argv[1]))
    return JS_ThrowTypeError(ctx, "argument 2 must be an object");
  /*
    if(!(fc = fetch_new(ctx)))
      return JS_ThrowOutOfMemory(ctx);*/

  if(!(cc = closure_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  args[0] = argv[0];
  args[1] = argc <= 1 ? JS_NewObject(ctx) : JS_DupValue(ctx, argv[1]);

  handlers[0] = JS_NewCClosure(ctx, &fetch_handler, 2, ON_HTTP, closure_dup(cc), closure_free);
  handlers[1] = JS_NewCClosure(ctx, &fetch_handler, 2, ON_ERROR, closure_dup(cc), closure_free);
  handlers[2] = JS_NewCClosure(ctx, &fetch_handler, 2, ON_CLOSE, closure_dup(cc), closure_free);
  handlers[3] = JS_NewCClosure(ctx, &fetch_handler, 3, ON_FD, closure_dup(cc), closure_free);

  JS_SetPropertyStr(ctx, args[1], "onHttp", handlers[0]);
  JS_SetPropertyStr(ctx, args[1], "onError", handlers[1]);
  JS_SetPropertyStr(ctx, args[1], "onClose", handlers[2]);
  JS_SetPropertyStr(ctx, args[1], "onFd", handlers[3]);
  JS_SetPropertyStr(ctx, args[1], "block", JS_FALSE);

  ret = minnet_client_closure(ctx, this_val, 2, args, 0, cc);

  JS_FreeValue(ctx, args[1]);

  DEBUG("%s url=%s client=%p\n", __func__, JS_ToCString(ctx, args[0]), cc->pointer);

  cc->pointer = client_dup(cc->pointer);

  return ret;
}

#ifndef MINNET_WEBSOCKET_H
#define MINNET_WEBSOCKET_H

#include "quickjs.h"

struct lws;
struct http_header;

/* class WebSocket */

typedef struct {
  struct lws* lwsi;
  size_t ref_count;
  struct http_header* header;
} MinnetWebsocket;

void minnet_ws_sslcert(JSContext*, struct lws_context_creation_info* info, JSValue options);

extern JSClassDef minnet_ws_class;
extern const JSCFunctionListEntry minnet_ws_proto_funcs[];
extern const size_t minnet_ws_proto_funcs_size;
extern JSClassID minnet_ws_class_id;

typedef struct minnet_ws_callback {
  JSContext* ctx;
  JSValueConst* this_obj;
  JSValue* func_obj;
} minnet_ws_callback;

#define GETCB(opt, cb_ptr)                                                                                                     \
  if(JS_IsFunction(ctx, opt)) {                                                                                                \
    struct minnet_ws_callback cb = {ctx, &this_val, &opt};                                                                     \
    cb_ptr = cb;                                                                                                               \
  }

static inline JSValue
create_websocket_obj(JSContext* ctx, struct lws* wsi) {
  MinnetWebsocket* res;
  JSValue ws_obj = JS_NewObjectClass(ctx, minnet_ws_class_id);

  if(JS_IsException(ws_obj))
    return JS_EXCEPTION;

  if(!(res = js_mallocz(ctx, sizeof(*res)))) {
    JS_FreeValue(ctx, ws_obj);
    return JS_EXCEPTION;
  }

  res->lwsi = wsi;
  res->ref_count = 1;

  JS_SetOpaque(ws_obj, res);

  lws_set_wsi_user(wsi, JS_VALUE_GET_OBJ(JS_DupValue(ctx, ws_obj)));

  return ws_obj;
}

static inline JSValue
get_websocket_obj(JSContext* ctx, struct lws* wsi) {
  JSObject* obj;

  if((obj = lws_wsi_user(wsi))) {
    JSValue ws_obj = JS_MKPTR(JS_TAG_OBJECT, obj);
    MinnetWebsocket* res = JS_GetOpaque2(ctx, ws_obj, minnet_ws_class_id);

    res->ref_count++;

    return JS_DupValue(ctx, ws_obj);
  }

  return create_websocket_obj(ctx, wsi);
}

static inline JSValue
call_websocket_callback(minnet_ws_callback* cb, int argc, JSValue* argv) {
  if(!cb->func_obj)
    return JS_UNDEFINED;
  return JS_Call(cb->ctx, *(cb->func_obj), *(cb->this_obj), argc, argv);
}

#endif /* MINNET_WEBSOCKET_H */
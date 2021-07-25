#ifndef MINNET_WEBSOCKET_H
#define MINNET_WEBSOCKET_H

#include <quickjs.h>

struct lws;
struct http_request;
struct http_response;

/* class WebSocket */

typedef struct socket {
  size_t ref_count;
  struct lws* lwsi;
  struct http_request* req;
  struct http_response* rsp;
} MinnetWebsocket;

MinnetWebsocket* minnet_ws_get(struct lws*, JSContext* ctx);
JSValue minnet_ws_object(JSContext*, struct lws* wsi);
JSValue minnet_ws_wrap(JSContext*, MinnetWebsocket* ws);
void minnet_ws_sslcert(JSContext*, struct lws_context_creation_info* info, JSValue options);

extern JSValue minnet_ws_proto;
extern JSClassDef minnet_ws_class;
extern const JSCFunctionListEntry minnet_ws_proto_funcs[];
extern const size_t minnet_ws_proto_funcs_size;
extern JSClassID minnet_ws_class_id;

static inline MinnetWebsocket*
lws_wsi_ws(struct lws* wsi) {
  JSObject* obj;
  MinnetWebsocket* ws = 0;

  // ws =lws_get_opaque_user_data(wsi);
  if((obj = lws_get_opaque_user_data(wsi)))
    ws = JS_GetOpaque(JS_MKPTR(JS_TAG_OBJECT, obj), minnet_ws_class_id);

  return ws;
}

#endif /* MINNET_WEBSOCKET_H */
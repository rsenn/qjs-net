#ifndef MINNET_WEBSOCKET_H
#define MINNET_WEBSOCKET_H

#include <quickjs.h>

struct lws;
struct http_request;
struct http_response;

/* class WebSocket */

typedef struct socket {
  struct lws* lwsi;
  JSValue request, response;
  size_t ref_count;
  JSValue handlers[2];
} MinnetWebsocket;

MinnetWebsocket* minnet_ws_from_wsi(struct lws*);
MinnetWebsocket* minnet_ws_get(struct lws*, JSContext* ctx);
JSValue minnet_ws_object(JSContext*, struct lws* wsi);
JSValue minnet_ws_wrap(JSContext*, MinnetWebsocket* ws);
void minnet_ws_sslcert(JSContext*, struct lws_context_creation_info* info, JSValue options);

extern JSValue minnet_ws_proto, minnet_ws_ctor;
extern JSClassDef minnet_ws_class;
extern const JSCFunctionListEntry minnet_ws_proto_funcs[], minnet_ws_static_funcs[], minnet_ws_proto_defs[];
extern const size_t minnet_ws_proto_funcs_size, minnet_ws_static_funcs_size, minnet_ws_proto_defs_size;
extern JSClassID minnet_ws_class_id;

static inline MinnetWebsocket*
minnet_ws_data(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_ws_class_id);
}

#endif /* MINNET_WEBSOCKET_H */

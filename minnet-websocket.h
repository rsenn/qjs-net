#ifndef MINNET_WEBSOCKET_H
#define MINNET_WEBSOCKET_H

#include "minnet.h"
#include <quickjs.h>

struct lws;
struct http_request;
struct http_response;

/* class WebSocket */

typedef struct socket {
  size_t ref_count;
  struct lws* lwsi;
  JSValue handlers[2];
  BOOL binary;
} MinnetWebsocket;

MinnetWebsocket* minnet_ws_from_wsi(struct lws*);
MinnetWebsocket* lws_ws(struct lws*, JSContext* ctx);
JSValue minnet_ws_object(JSContext*, struct lws* wsi);
JSValue minnet_ws_wrap(JSContext*, struct lws* wsi);
void minnet_ws_sslcert(JSContext*, struct lws_context_creation_info* info, JSValue options);
JSValue minnet_ws_constructor(JSContext*, JSValue new_target, int argc, JSValue argv[]);

extern THREAD_LOCAL JSClassID minnet_ws_class_id;
extern THREAD_LOCAL JSValue minnet_ws_proto, minnet_ws_ctor;
extern JSClassDef minnet_ws_class;
extern const JSCFunctionListEntry minnet_ws_proto_funcs[], minnet_ws_static_funcs[], minnet_ws_proto_defs[];
extern const size_t minnet_ws_proto_funcs_size, minnet_ws_static_funcs_size, minnet_ws_proto_defs_size;

struct __attribute__((packed)) wsi_opaque_user_data {
  JSObject* obj;
  struct socket* ws;
  struct http_request* req;
  int64_t serial;
  MinnetStatus status : 8;
  int error;
  MinnetPollFd pfd;
};

static inline struct wsi_opaque_user_data*
lws_opaque(struct lws* wsi, JSContext* ctx) {
  static int64_t opaque_serial;
  struct wsi_opaque_user_data* opaque;

  if((opaque = lws_get_opaque_user_data(wsi)))
    return opaque;

  opaque = js_mallocz(ctx, sizeof(struct wsi_opaque_user_data));
  opaque->serial = ++opaque_serial;

  lws_set_opaque_user_data(wsi, opaque);
  return opaque;
}

static inline int
ws_fd(const MinnetWebsocket* ws) {
  return lws_get_socket_fd(lws_get_network_wsi(ws->lwsi));
}

static inline int
ws_lws(const MinnetWebsocket* ws) {
  return ws->lwsi;
}

static inline struct wsi_opaque_user_data*
ws_opaque(const MinnetWebsocket* ws) {
  return lws_get_opaque_user_data(ws->lwsi);
}

static inline MinnetWebsocket*
minnet_ws_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_ws_class_id);
}

static inline MinnetWebsocket*
minnet_ws_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_ws_class_id);
}

#endif /* MINNET_WEBSOCKET_H */

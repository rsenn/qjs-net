#ifndef MINNET_WEBSOCKET_H
#define MINNET_WEBSOCKET_H

#if(defined(HAVE_WINSOCK2_H) || defined(WIN32) || defined(WIN64) || defined(__MINGW32__) || defined(__MINGW64__)) && !defined(__MSYS__)

#warning winsock2
#include <winsock2.h>
#if 0
struct pollfd {
  int fd;
  short events, revents;
};
#endif
#endif

#include "minnet.h"
#include <quickjs.h>

struct lws;
struct http_request;
struct http_response;
struct wsi_opaque_user_data;

/* class WebSocket */

typedef struct socket {
  size_t ref_count;
  struct lws* lwsi;
  // struct wsi_opaque_user_data* opaque;
} MinnetWebsocket;

extern int64_t ws_serial;

MinnetWebsocket* ws_new(struct lws*, JSContext*);
MinnetWebsocket* ws_from_wsi2(struct lws*, JSContext*);
struct wsi_opaque_user_data* lws_opaque(struct lws* wsi, JSContext* ctx);
JSValue minnet_ws_object(JSContext*, struct lws*);
JSValue minnet_ws_wrap(JSContext*, struct lws*);
JSValue minnet_ws_constructor(JSContext*, JSValue, int, JSValue[]);

extern THREAD_LOCAL JSClassID minnet_ws_class_id;
extern THREAD_LOCAL JSValue minnet_ws_proto, minnet_ws_ctor;
extern JSClassDef minnet_ws_class;
extern const JSCFunctionListEntry minnet_ws_proto_funcs[], minnet_ws_static_funcs[], minnet_ws_proto_defs[];
extern const size_t minnet_ws_proto_funcs_size, minnet_ws_static_funcs_size, minnet_ws_proto_defs_size;

struct wsi_opaque_user_data {
  JSObject* obj;
  struct socket* ws;
  struct http_request* req;
  struct session_data* sess;
  JSValue handler;
  int64_t serial;
  MinnetStatus status;
  struct pollfd poll;
  int error;
  BOOL binary;
};

static inline struct session_data*
lws_session(struct lws* wsi) {
  struct wsi_opaque_user_data* opaque;

  if((opaque = lws_get_opaque_user_data(wsi)))
    return opaque->sess;

  return 0;
}

static inline struct wsi_opaque_user_data*
ws_opaque(MinnetWebsocket* ws) {
  return lws_get_opaque_user_data(ws->lwsi);
}

static inline struct session_data*
ws_session(MinnetWebsocket* ws) {
  return lws_session(ws->lwsi);
}

static inline MinnetWebsocket*
ws_from_wsi(struct lws* wsi) {
  struct wsi_opaque_user_data* opaque;
  return (opaque = lws_get_opaque_user_data(wsi)) ? opaque->ws : 0;
}

/*
static inline int
ws_lws(const MinnetWebsocket* ws) {
 return ws->lwsi;
}

static inline struct wsi_opaque_user_data*
ws_opaque(const MinnetWebsocket* ws) {
 return lws_get_opaque_user_data(ws->lwsi);
}*/

static inline MinnetWebsocket*
minnet_ws_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_ws_class_id);
}

static inline MinnetWebsocket*
minnet_ws_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_ws_class_id);
}

#endif /* MINNET_WEBSOCKET_H */

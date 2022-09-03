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
#include "minnet-ringbuffer.h"
#include <quickjs.h>

struct lws;
struct http_request;
struct http_response;
struct wsi_opaque_user_data;

/* class WebSocket */

typedef struct socket {
  int ref_count;
  struct lws* lwsi;
  // struct wsi_opaque_user_data* opaque;
  MinnetRingbuffer sendq;
} MinnetWebsocket;

extern int64_t ws_serial;

void opaque_free_rt(struct wsi_opaque_user_data*, JSRuntime*);
void opaque_free(struct wsi_opaque_user_data*, JSContext*);
JSValue minnet_ws_new(JSContext*, struct lws*);
MinnetWebsocket* ws_new(struct lws*, JSContext*);
void ws_clear_rt(MinnetWebsocket*, JSRuntime*);
void ws_clear(MinnetWebsocket*, JSContext*);
void ws_free_rt(MinnetWebsocket*, JSRuntime*);
void ws_free(MinnetWebsocket*, JSContext*);
MinnetWebsocket* ws_dup(MinnetWebsocket*);
struct wsi_opaque_user_data* lws_opaque(struct lws*, JSContext*);
JSValue minnet_ws_wrap(JSContext*, MinnetWebsocket*);
JSValue minnet_ws_fromwsi(JSContext*, struct lws*);
JSValue minnet_ws_constructor(JSContext*, JSValue, int, JSValue argv[]);

extern THREAD_LOCAL JSClassID minnet_ws_class_id;
extern THREAD_LOCAL JSValue minnet_ws_proto, minnet_ws_ctor;
extern JSClassDef minnet_ws_class;
extern const JSCFunctionListEntry minnet_ws_proto_funcs[], minnet_ws_static_funcs[], minnet_ws_proto_defs[];
extern const size_t minnet_ws_proto_funcs_size, minnet_ws_static_funcs_size, minnet_ws_proto_defs_size;

enum socket_state {
  CONNECTING = 0,
  OPEN = 1,
  CLOSING = 2,
  CLOSED = 3,
};

struct wsi_opaque_user_data {
  int ref_count;
  struct socket* ws;
  struct http_request* req;
  struct http_response* resp;
  struct session_data* sess;
  JSValue handler;
  int64_t serial;
  enum socket_state status;
  struct pollfd poll;
  int error;
  BOOL binary;
  struct list_head link;
  struct form_parser* form_parser;
};

static inline struct wsi_opaque_user_data*
opaque_dup(struct wsi_opaque_user_data* opaque) {
  opaque->ref_count++;
  return opaque;
}

static inline struct session_data*
lws_session(struct lws* wsi) {
  struct wsi_opaque_user_data* opaque;

  if((opaque = lws_get_opaque_user_data(wsi)))
    return opaque->sess;

  return 0;
}

static inline struct wsi_opaque_user_data*
ws_opaque(MinnetWebsocket* ws) {
  return ws->lwsi ? lws_get_opaque_user_data(ws->lwsi) : 0;
}

static inline struct session_data*
ws_session(MinnetWebsocket* ws) {
  return ws->lwsi ? lws_session(ws->lwsi) : 0;
}

static inline MinnetWebsocket*
ws_from_wsi(struct lws* wsi) {
  struct wsi_opaque_user_data* opaque;
  return (opaque = lws_get_opaque_user_data(wsi)) ? opaque->ws : 0;
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

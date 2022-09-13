#ifndef WJSNET_LIBQJSNET_LIB_WS_H
#define WJSNET_LIBQJSNET_LIB_WS_H

#include <libwebsockets.h> // for lws_get_opaque_user_data
#include <quickjs.h>       // for JSContext, JSRuntime
#include "ringbuffer.h"    // for struct ringbuffer
#include "cutils.h"        // for BOOL
#include "opaque.h"        // for wsi_opaque_user_data

#if(defined(HAVE_WINSOCK2_H) || defined(WIN32) || defined(WIN64) || defined(__MINGW32__) || defined(__MINGW64__)) && !defined(__MSYS__)
#warning winsock2
#include <winsock2.h>
#endif

struct lws;
struct http_request;
struct http_response;

struct socket {
  int ref_count;
  struct lws* lwsi;
  // struct wsi_opaque_user_data* opaque;
  struct ringbuffer sendq;
};

struct socket* ws_new(struct lws*, JSContext*);
void ws_clear_rt(struct socket*, JSRuntime*);
void ws_clear(struct socket*, JSContext*);
void ws_free_rt(struct socket*, JSRuntime*);
void ws_free(struct socket*, JSContext*);
struct socket* ws_dup(struct socket*);
int ws_write(struct socket* ws, BOOL binary, JSContext* ctx);

static inline struct session_data*
lws_session(struct lws* wsi) {
  struct wsi_opaque_user_data* opaque;

  if((opaque = lws_get_opaque_user_data(wsi)))
    return opaque->sess;

  return 0;
}

static inline struct wsi_opaque_user_data*
ws_opaque(struct socket* ws) {
  return ws->lwsi ? lws_get_opaque_user_data(ws->lwsi) : 0;
}

static inline struct session_data*
ws_session(struct socket* ws) {
  return ws->lwsi ? lws_session(ws->lwsi) : 0;
}

static inline struct socket*
ws_from_wsi(struct lws* wsi) {
  struct wsi_opaque_user_data* opaque;
  return (opaque = lws_get_opaque_user_data(wsi)) ? opaque->ws : 0;
}

#endif /* WJSNET_LIBQJSNET_LIB_WS_H */

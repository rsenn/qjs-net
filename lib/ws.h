/**
 * @file ws.h
 */
#ifndef QJSNET_LIB_WS_H
#define QJSNET_LIB_WS_H

#include <libwebsockets.h>
#include "opaque.h"
#include "js-utils.h"
#include "queue.h"

#if(defined(HAVE_WINSOCK2_H) || defined(WIN32) || defined(WIN64) || defined(__MINGW32__) || defined(__MINGW64__)) && !defined(__MSYS__)
#include <winsock2.h>
#endif

struct lws;
struct http_request;
struct http_response;

struct socket {
  int ref_count;
  struct lws* lwsi;
  int fd;
  BOOL raw : 1, binary : 1;
};

struct socket* ws_new(struct lws*, JSContext* ctx);
void ws_clear(struct socket*, JSRuntime* rt);
void ws_free(struct socket*, JSRuntime* rt);
struct socket* ws_dup(struct socket*);
QueueItem* ws_enqueue(struct socket*, ByteBlock);
Queue* ws_queue(struct socket* ws);

static inline struct session_data*
lws_session(struct lws* wsi) {
  struct wsi_opaque_user_data* opaque;

  if((opaque = lws_get_opaque_user_data(wsi)))
    return opaque->sess;

  return 0;
}

static inline struct wsi_opaque_user_data*
ws_opaque(struct socket* ws) {
  struct wsi_opaque_user_data* opaque = ws->lwsi ? lws_get_opaque_user_data(ws->lwsi) : 0;

  return opaque;
}

static inline struct session_data*
ws_session(struct socket* ws) {
  return ws->lwsi ? lws_session(ws->lwsi) : 0;
}

static inline int
ws_fd(struct socket* ws) {
  return ws->lwsi ? lws_get_socket_fd(lws_get_network_wsi(ws->lwsi)) : 0;
}

static inline struct socket*
ws_from_wsi(struct lws* wsi) {
  struct wsi_opaque_user_data* opaque;
  return ((opaque = lws_get_opaque_user_data(wsi)) && opaque_valid(opaque)) ? opaque->ws : 0;
}

static inline QueueItem*
ws_send(struct socket* ws, const void* data, size_t size, JSContext* ctx) {
  return ws_enqueue(ws, block_copy(data, size));
}

#endif /* QJSNET_LIB_WS_H */

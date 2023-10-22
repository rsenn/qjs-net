/**
 * @file ws.c
 */
#include "ws.h"
#include "buffer.h"
#include "js-utils.h"
#include "opaque.h"
#include "session.h"
#include "ringbuffer.h"
#include <strings.h>
#include <assert.h>
#include <libwebsockets.h>

struct socket*
ws_new(struct lws* wsi, JSContext* ctx) {
  struct socket* ws;
  struct wsi_opaque_user_data* opaque;

  if(!(ws = js_mallocz(ctx, sizeof(struct socket))))
    return 0;

  ws->lwsi = wsi;
  ws->ref_count = 2;
  ws->fd = lws_get_socket_fd(lws_get_network_wsi(wsi));
  ws->raw = FALSE;
  ws->binary = TRUE;

  // ringbuffer_init2(&ws->sendq, sizeof(ByteBlock), 65536 * 2);

  if((opaque = lws_opaque(wsi, ctx))) {
    opaque->ws = ws;
    opaque->status = 0;
    opaque->fd = ws->fd;
  }

  return ws;
}

void
ws_clear(struct socket* ws, JSRuntime* rt) {
  struct lws* wsi = ws->lwsi;

  ws->lwsi = 0;

  if(wsi) {
    struct wsi_opaque_user_data* opaque;

    if((opaque = lws_get_opaque_user_data(wsi))) {
      lws_set_opaque_user_data(wsi, 0);
      opaque_free(opaque, rt);

      /*  if(status < CLOSING)
          lws_close_free_wsi(wsi, LWS_CLOSE_STATUS_NOSTATUS, __func__);*/
    }
  }

  // ringbuffer_zero(&ws->sendq);
}

void
ws_free(struct socket* ws, JSRuntime* rt) {
  if(--ws->ref_count == 0) {
    ws_clear(ws, rt);
    js_free_rt(rt, ws);
  }
}

struct socket*
ws_dup(struct socket* ws) {
  ++ws->ref_count;
  return ws;
}

Queue*
ws_queue(struct socket* ws) {
  struct wsi_opaque_user_data* opaque;
  struct session_data* session;

  if((opaque = ws_opaque(ws)))
    if((session = opaque->sess))
      return &session->sendq;
  return 0;
}

QueueItem*
ws_enqueue(struct socket* ws, ByteBlock chunk) {
  struct wsi_opaque_user_data* opaque;
  struct session_data* session;
  QueueItem* item;

  if((opaque = ws_opaque(ws)))
    if((session = opaque->sess))
      if((item = queue_add(&session->sendq, chunk)))
        session_want_write(session, ws->lwsi);

  return item;
}

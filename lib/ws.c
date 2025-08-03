/**
 * @file ws.c
 */
#include "ws.h"
#include "buffer.h"
#include "js-utils.h"
#include "opaque.h"
#include "session.h"
#include "ringbuffer.h"
#include "queue.h"
#include <strings.h>
#include <assert.h>

struct socket*
ws_new(struct lws* wsi, JSContext* ctx) {
  struct socket* ws;
  struct wsi_opaque_user_data* opaque;

  if(!(ws = js_mallocz(ctx, sizeof(struct socket))))
    return 0;

  ws->lwsi = wsi;
  ws->ref_count = 2;
  ws->raw = FALSE;
  ws->binary = TRUE;

  if((opaque = opaque_from_wsi(wsi, ctx)))
    opaque->ws = ws;

  ws->fd = opaque->fd;

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
    }
  }
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

QueueItem*
ws_send(struct socket* ws, const void* data, size_t size, JSContext* ctx) {
  QueueItem* item = ws_enqueue(ws, block_copy(data, size));

  return item;
}

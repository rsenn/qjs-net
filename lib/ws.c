#include "ws.h"
#include "buffer.h"
#include "jsutils.h"
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
  // ringbuffer_init2(&ws->sendq, sizeof(ByteBlock), 65536 * 2);

  if((opaque = lws_opaque(wsi, ctx))) {
    opaque->ws = ws;
    opaque->status = 0;
  }

  return ws;
}

void
ws_clear_rt(struct socket* ws, JSRuntime* rt) {
  struct lws* wsi = ws->lwsi;

  ws->lwsi = 0;

  if(wsi) {
    struct wsi_opaque_user_data* opaque;

    if((opaque = lws_get_opaque_user_data(wsi))) {
      lws_set_opaque_user_data(wsi, 0);
      opaque_free_rt(opaque, rt);

      /*  if(status < CLOSING)
          lws_close_free_wsi(wsi, LWS_CLOSE_STATUS_NOSTATUS, __func__);*/
    }
  }

  // ringbuffer_zero(&ws->sendq);
}

void
ws_clear(struct socket* ws, JSContext* ctx) {
  ws_clear_rt(ws, JS_GetRuntime(ctx));
}

void
ws_free_rt(struct socket* ws, JSRuntime* rt) {
  if(--ws->ref_count == 0) {
    ws_clear_rt(ws, rt);
    js_free_rt(rt, ws);
  }
}

void
ws_free(struct socket* ws, JSContext* ctx) {
  if(--ws->ref_count == 0) {
    ws_clear(ws, ctx);
    js_free(ctx, ws);
  }
}

struct socket*
ws_dup(struct socket* ws) {
  ++ws->ref_count;
  return ws;
}

QueueItem*
ws_enqueue(struct socket* ws, ByteBlock chunk) {
  struct wsi_opaque_user_data* opaque;
  QueueItem* item = 0;

  if((opaque = lws_get_opaque_user_data(ws->lwsi))) {
    struct session_data* session = opaque->sess;

    if((item = queue_add(&session->sendq, chunk))) {

      session_want_write(session, ws->lwsi);
    }
  }

  return item;
}

QueueItem*
ws_send(struct socket* ws, const void* data, size_t size, JSContext* ctx) {
  ByteBlock chunk = block_copy(data, size);

  return ws_enqueue(ws, chunk);
}

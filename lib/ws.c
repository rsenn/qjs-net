#include "buffer.h"
#include "jsutils.h" 
#include "minnet.h"
#include "opaque.h"
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
  ringbuffer_init2(&ws->sendq, sizeof(ByteBlock), 65536 * 2);

  if((opaque = lws_opaque(wsi, ctx))) {
    opaque->ws = ws;
    opaque->status = 0;
    opaque->handler = JS_NULL;
    /*opaque->handlers[0] = JS_NULL;
    opaque->handlers[1] = JS_NULL;*/
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

  ringbuffer_zero(&ws->sendq);
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

int
ws_write(struct socket* ws, BOOL binary, JSContext* ctx) {
  int ret = 0;
  size_t size;

  if((size = ringbuffer_size(&ws->sendq))) {
    ByteBlock buf;
    ringbuffer_consume(&ws->sendq, &buf, 1);

    ret = lws_write(ws->lwsi, block_BEGIN(&buf), block_SIZE(&buf), binary ? LWS_WRITE_BINARY : LWS_WRITE_TEXT);

    block_free(&buf, ctx);
  }

  if(size > 1)
    lws_callback_on_writable(ws->lwsi);

  return ret;
}

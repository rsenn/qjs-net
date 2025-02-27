/**
 * @file opaque.c
 */
#include "opaque.h"
#include "ws.h"
#include "request.h"
#include "response.h"
#include "formparser.h"
#include <assert.h>

THREAD_LOCAL int64_t opaque_serial = 0;
THREAD_LOCAL struct list_head opaque_list = {0, 0};

void
opaque_clear(struct wsi_opaque_user_data* opaque, JSRuntime* rt) {
  if(opaque->ws) {
    struct socket* ws = opaque->ws;
    opaque->ws = 0;
    ws_free(ws, rt);
  }

  if(opaque->req) {
    Request* req = opaque->req;
    opaque->req = 0;
    request_free(req, rt);
  }

  if(opaque->resp) {
    Response* resp = opaque->resp;
    opaque->resp = 0;
    response_free(resp, rt);
  }

  if(opaque->form_parser) {
    formparser_free(opaque->form_parser, rt);
    opaque->form_parser = 0;
  }

  JS_FreeValueRT(rt, opaque->handlers[0]);
  opaque->handlers[0] = JS_NULL;

  JS_FreeValueRT(rt, opaque->handlers[1]);
  opaque->handlers[1] = JS_NULL;
}

void
opaque_free(struct wsi_opaque_user_data* opaque, JSRuntime* rt) {
  if(--opaque->ref_count == 0) {
    opaque_clear(opaque, rt);

    assert(opaque->link.next);
    list_del(&opaque->link);

    js_free_rt(rt, opaque);
  }
}

struct wsi_opaque_user_data*
opaque_new(JSContext* ctx) {
  struct wsi_opaque_user_data* opaque;

  if((opaque = js_mallocz(ctx, sizeof(struct wsi_opaque_user_data)))) {
    opaque->serial = ++opaque_serial;
    opaque->status = CONNECTING;
    opaque->ref_count = 1;
    opaque->fd = -1;
    opaque->handlers[0] = JS_NULL;
    opaque->handlers[1] = JS_NULL;

    if(opaque_list.prev == NULL)
      init_list_head(&opaque_list);

    list_add(&opaque->link, &opaque_list);
  }

  return opaque;
}

struct wsi_opaque_user_data*
opaque_from_wsi(struct lws* wsi, JSContext* ctx) {
  struct wsi_opaque_user_data* opaque;

  if((opaque = lws_get_opaque_user_data(wsi)))
    return opaque;

  assert(ctx);

  opaque = opaque_new(ctx);

  opaque->fd = lws_get_socket_fd(lws_get_network_wsi(wsi));

  lws_set_opaque_user_data(wsi, opaque);
  return opaque;
}

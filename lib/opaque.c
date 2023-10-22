/**
 * @file opaque.c
 */
#include "opaque.h"
#include "ws.h"
#include "request.h"
#include "response.h"
#include "formparser.h"
#include <assert.h>

// static THREAD_LOCAL void* prev_ptr = 0;
THREAD_LOCAL int64_t serial = 0;
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
}

void
opaque_free(struct wsi_opaque_user_data* opaque, JSRuntime* rt) {
  opaque_clear(opaque, rt);

  if(--opaque->ref_count == 0) {
    assert(opaque->link.next);
    list_del(&opaque->link);

    js_free_rt(rt, opaque);
  }
}

struct wsi_opaque_user_data*
opaque_new(JSContext* ctx) {
  struct wsi_opaque_user_data* opaque;

  if((opaque = js_mallocz(ctx, sizeof(struct wsi_opaque_user_data)))) {
    opaque->serial = ++serial;
    opaque->status = CONNECTING;
    opaque->ref_count = 1;
    opaque->fd = -1;

    if(opaque_list.prev == NULL)
      init_list_head(&opaque_list);

    list_add(&opaque->link, &opaque_list);
  }

  return opaque;
}

struct wsi_opaque_user_data*
lws_opaque(struct lws* wsi, JSContext* ctx) {
  struct wsi_opaque_user_data* opaque;

  if((opaque = lws_get_opaque_user_data(wsi)))
    return opaque;

  assert(ctx);

  opaque = opaque_new(ctx);

  lws_set_opaque_user_data(wsi, opaque);
  return opaque;
}

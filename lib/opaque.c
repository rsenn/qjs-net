#include "opaque.h"
#include <assert.h>

static THREAD_LOCAL void* prev_ptr = 0;
THREAD_LOCAL int64_t serial = 0;
THREAD_LOCAL struct list_head sockets = {0, 0};

void
opaque_clear_rt(struct wsi_opaque_user_data* opaque, JSRuntime* rt) {

  // printf("%s opaque=%p link=[%p, %p]\n", __func__, opaque, opaque->link.next, opaque->link.prev);

  prev_ptr = opaque;

  if(opaque->ws) {
    struct socket* ws = opaque->ws;
    opaque->ws = 0;
    ws_free_rt(ws, rt);
  }
  if(opaque->req) {
    struct http_request* req = opaque->req;
    opaque->req = 0;
    request_free_rt(req, rt);
  }
  if(opaque->resp) {
    struct http_response* resp = opaque->resp;
    opaque->resp = 0;
    response_free_rt(resp, rt);
  }

  assert(opaque->link.next);
  list_del(&opaque->link);
}

void
opaque_free_rt(struct wsi_opaque_user_data* opaque, JSRuntime* rt) {
  opaque_clear_rt(opaque, rt);

  if(--opaque->ref_count == 0)
    js_free_rt(rt, opaque);
}

void
opaque_clear(struct wsi_opaque_user_data* opaque, JSContext* ctx) {
  opaque_clear_rt(opaque, JS_GetRuntime(ctx));
}

void
opaque_free(struct wsi_opaque_user_data* opaque, JSContext* ctx) {
  opaque_clear(opaque, ctx);

  if(--opaque->ref_count == 0)

    js_free(ctx, opaque);
}

struct wsi_opaque_user_data*
opaque_new(JSContext* ctx) {
  struct wsi_opaque_user_data* opaque;

  if((opaque = js_mallocz(ctx, sizeof(struct wsi_opaque_user_data)))) {
    opaque->serial = ++serial;
    opaque->status = CONNECTING;
    opaque->ref_count = 1;

    if(sockets.prev == NULL)
      init_list_head(&sockets);

    list_add(&opaque->link, &sockets);
  }

  return opaque;
}

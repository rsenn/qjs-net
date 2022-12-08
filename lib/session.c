#include "session.h"
#include "opaque.h"
#include "ringbuffer.h"
#include "jsutils.h"
#include "../minnet-websocket.h"

static THREAD_LOCAL uint32_t session_serial = 0;
THREAD_LOCAL struct list_head session_list = {0, 0};

void
session_zero(struct session_data* session) {
  session->ws_obj = JS_NULL;
  session->req_obj = JS_NULL;
  session->resp_obj = JS_NULL;
  session->mount = 0;
  session->proxy = 0;
  session->generator = JS_NULL;
  session->next = JS_NULL;
  session->serial = ++session_serial;
  // session->h2 = FALSE;
  session->in_body = FALSE;
  session->response_sent = FALSE;
  session->server = NULL;
  session->client = NULL;
  // buffer_init(&session->send_buf, 0, 0);
  session->link.prev = session->link.next = NULL;
  queue_zero(&session->sendq);
}

void
session_add(struct session_data* session) {
  if(session_list.prev == NULL)
    init_list_head(&session_list);

  list_add(&session->link, &session_list);
}

void
session_remove(struct session_data* session) {
  list_del(&session->link);
}

void
session_clear(struct session_data* session, JSContext* ctx) {
  session_clear_rt(session, JS_GetRuntime(ctx));
}

void
session_clear_rt(struct session_data* session, JSRuntime* rt) {

  JS_FreeValueRT(rt, session->ws_obj);
  session->ws_obj = JS_UNDEFINED;

  JS_FreeValueRT(rt, session->req_obj);
  session->req_obj = JS_UNDEFINED;
  JS_FreeValueRT(rt, session->resp_obj);
  session->resp_obj = JS_UNDEFINED;
  JS_FreeValueRT(rt, session->generator);
  session->generator = JS_UNDEFINED;
  JS_FreeValueRT(rt, session->next);
  session->next = JS_UNDEFINED;

  if(queue_size(&session->sendq)) {
    /* while(queue_size(&session->sendq)) {
       JSBuffer buf;
       if(ringbuffer_consume(session->sendq, &buf, 1))
        1 js_buffer_free_rt(&buf, rt);
     }*/

    queue_clear_rt(&session->sendq, rt);
  }
  // printf("%s #%i %p\n", __func__, session->serial, session);
}

JSValue
session_object(struct wsi_opaque_user_data* opaque, JSContext* ctx) {
  JSValue ret;
  ret = JS_NewArray(ctx);

  JS_SetPropertyUint32(ctx, ret, 0, opaque->serial ? JS_NewInt32(ctx, opaque->serial) : JS_NULL);

  if(opaque->sess) {
    JS_SetPropertyUint32(ctx, ret, 1, JS_DupValue(ctx, opaque->sess->ws_obj));
    JS_SetPropertyUint32(ctx, ret, 2, JS_DupValue(ctx, opaque->sess->req_obj));
    JS_SetPropertyUint32(ctx, ret, 3, JS_DupValue(ctx, opaque->sess->resp_obj));
  }
  return ret;
}

int
session_writable(struct session_data* session, BOOL binary, JSContext* ctx) {

  int ret = 0;
  size_t size;
  struct socket* ws = minnet_ws_data(session->ws_obj);

  if((size = queue_size(&session->sendq)) > 0) {
    ByteBlock chunk;
    BOOL done = FALSE;

    chunk = queue_next(&session->sendq, &done);

    ret = lws_write(ws->lwsi, block_BEGIN(&chunk), block_SIZE(&chunk), binary ? LWS_WRITE_BINARY : LWS_WRITE_TEXT);

    block_free(&chunk, ctx);
    //  JS_FreeValue(ctx, chunk);
  }
  if(queue_size(&session->sendq) > 0)
    lws_callback_on_writable(ws->lwsi);
  return ret;
}

#include "session.h"
#include "request.h"
#include "response.h"
#include "opaque.h"
#include "ringbuffer.h"
#include "jsutils.h"
#include "ws.h"
#include "context.h"
#include "lws-utils.h"
#include <assert.h>

struct socket* minnet_ws_data(JSValueConst);
Request* minnet_request_data(JSValueConst);
Response* minnet_response_data(JSValueConst);

extern Response* minnet_response_data(JSValueConst);

void
session_zero(struct session_data* session) {
  session->ws_obj = JS_NULL;
  session->req_obj = JS_NULL;
  session->resp_obj = JS_NULL;
  session->mount = 0;
  session->proxy = 0;
  session->generator = JS_NULL;
  session->next = JS_NULL;
  session->in_body = FALSE;
  session->response_sent = FALSE;
  session->want_write = FALSE;
  session->wait_resolve = FALSE;
  session->generator_run = FALSE;
  session->callback_count = 0;
  session->server = NULL;
  session->client = NULL;
  session->callback = NULL;
  session->wait_resolve_ptr = NULL;

  queue_zero(&session->sendq);
}

void
session_clear(struct session_data* session, JSRuntime* rt) {

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

  if(session->wait_resolve_ptr) {
    *session->wait_resolve_ptr = 0;
    session->wait_resolve_ptr = 0;
  }

  if(queue_size(&session->sendq))
    queue_clear_rt(&session->sendq, rt);
}

JSValue
session_object(struct session_data* session, JSContext* ctx) {
  JSValue ret;
  ret = JS_NewArray(ctx);

  JS_SetPropertyUint32(ctx, ret, 1, JS_DupValue(ctx, session->ws_obj));
  JS_SetPropertyUint32(ctx, ret, 2, JS_DupValue(ctx, session->req_obj));
  JS_SetPropertyUint32(ctx, ret, 3, JS_DupValue(ctx, session->resp_obj));

  return ret;
}

int
session_want_write(struct session_data* session, struct lws* wsi) {
  assert(!session->want_write);

  lws_callback_on_writable(wsi);
  session->want_write = TRUE;

  // printf("%s\n", __func__);
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

    block_free(&chunk);
  }

  if(queue_bytes(&session->sendq) > 0)
    session_want_write(session, ws->lwsi);

  return ret;
}

JSValue
session_callback(struct session_data* session, JSCallback* cb, struct context* context) {
  int ret = 0;
  JSValue body;

  context_exception(context, (body = callback_emit_this(cb, session->ws_obj, 2, &session->req_obj)));

  /* DBG("body=%s", JS_ToCString(cb->ctx, body));

   if(JS_IsException(body))
     return 0;

   if(!(ret = session_generator(session, body, cb->ctx))) {
     JS_FreeValue(cb->ctx, body);
     body = JS_GetPropertyStr(cb->ctx, session->resp_obj, "body");

     ret = session_generator(session, body, cb->ctx);
   }

   JS_FreeValue(cb->ctx, body);*/
  return body;
}

int
session_generator(struct session_data* session, JSValue generator, JSContext* ctx) {
  JSValue this = session->resp_obj;
  JSAtom prop;
  int ret = 0;
  BOOL async = FALSE;

  if((prop = js_iterable_method(ctx, generator, &async)) > 0) {
    JSValue tmp = JS_GetProperty(ctx, generator, prop);
    JS_FreeAtom(ctx, prop);
    this = generator;
    generator = tmp;
  }

  if(JS_IsFunction(ctx, generator)) {
    if(!async)
      async = js_function_is_async(ctx, generator);
    generator = JS_Call(ctx, generator, this, 2, &session->req_obj);
  }

  ret = !js_is_nullish(generator);

  session->generator = ret ? JS_DupValue(ctx, generator) : JS_NULL;
  session->next = JS_NULL;

  return ret ? (async ? 2 : 1) : 0;
}

struct wsi_opaque_user_data*
session_opaque(struct session_data* sess) {
  struct socket* ws;

  if((ws = session_ws(sess))) {
    assert(ws->lwsi);
    return lws_get_opaque_user_data(ws->lwsi);
  }

  return 0;
}

Response*
session_response(struct session_data* sess) {
  if(JS_IsObject(sess->resp_obj))
    return minnet_response_data(sess->resp_obj);
  return 0;
}

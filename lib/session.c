/**
 * @file session.c
 */
#include "session.h"
#include "request.h"
#include "response.h"
#include "opaque.h"
#include "ringbuffer.h"
#include "js-utils.h"
#include "ws.h"
#include "context.h"
#include "lws-utils.h"
#include <assert.h>

static void
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
  session->callback = NULL;
  session->wait_resolve_ptr = NULL;

  queue_zero(&session->sendq);
}

void
session_init(struct session_data* session, struct context* context) {
  session_zero(session);
  session->context = context;
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

  queue_clear(&session->sendq, rt);
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

void
session_want_write(struct session_data* session, struct lws* wsi) {
  if(!session->want_write) {
    lws_callback_on_writable(wsi);
    session->want_write = TRUE;
  }
}

int
session_writable(struct session_data* session, struct lws* wsi, JSContext* ctx) {
  int ret = 0;
  size_t size;

  session->want_write = FALSE;

  if((size = queue_size(&session->sendq)) > 0) {
    ByteBlock chunk;
    BOOL done = FALSE, binary = FALSE;

    chunk = queue_next(&session->sendq, &done, &binary);

    ret = lws_write(wsi, block_BEGIN(&chunk), block_SIZE(&chunk), binary ? LWS_WRITE_BINARY : LWS_WRITE_TEXT);

    block_free(&chunk);
  }

  if(queue_bytes(&session->sendq) > 0)
    session_want_write(session, wsi);

  return ret;
}

FunctionType
session_callback(struct session_data* session, JSCallback* cb) {
  int ret = 0;
  JSValue body, this = session->ws_obj;
  BOOL async = FALSE, is_generator = js_function_is_generator(cb->ctx, cb->func_obj, &async);

  body = context_exception(session->context, callback_emit_this(cb, session->ws_obj, JS_IsObject(session->resp_obj) ? 2 : 1, &session->req_obj));

  if(is_generator) {
    ret = session_generator(session, body, this);
    JS_FreeValue(cb->ctx, body);
  } else {
    session->generator = body;
    ret = js_is_promise(cb->ctx, body) ? ASYNC : SYNC;
  }

  if(!ret) {
    body = JS_GetPropertyStr(cb->ctx, session->resp_obj, "body");
    this = session->resp_obj;

    ret = session_generator(session, body, this);
    JS_FreeValue(cb->ctx, body);
  }

  return ret;
}

FunctionType
session_generator(struct session_data* session, JSValue generator, JSValueConst this) {
  JSContext* ctx = session->context->js;
  JSAtom prop;
  BOOL async = FALSE;

  if(!js_is_iterator(ctx, generator)) {
    if((prop = js_iterable_method(ctx, generator, &async)) > 0) {
      JSValue tmp = JS_GetProperty(ctx, generator, prop);
      JS_FreeAtom(ctx, prop);
      this = generator;
      generator = tmp;
    }
  }

  if(!js_is_iterator(ctx, generator)) {
    if(JS_IsFunction(ctx, generator)) {
      if(!async)
        async = js_function_is_async(ctx, generator);
      context_exception(session->context, (generator = JS_Call(ctx, generator, this, 2, &session->req_obj)));
    }
  }

  if(!async)
    async = js_is_async_generator(ctx, generator);

  session->generator = JS_DupValue(ctx, generator);
  session->next = JS_NULL;

  return JS_IsObject(generator) ? (async ? ASYNC_GENERATOR : GENERATOR) : 0;
}

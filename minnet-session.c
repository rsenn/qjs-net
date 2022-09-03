#include "minnet-session.h"

void
session_zero(MinnetSession* session) {
  memset(session, 0, sizeof(MinnetSession));
  session->serial = -1;
  session->ws_obj = JS_NULL;
  session->req_obj = JS_NULL;
  session->resp_obj = JS_NULL;
  session->generator = JS_NULL;
  session->next = JS_NULL;

  session->serial = ++session_serial;

  // list_add(&session->link, &minnet_sessions);

  // printf("%s #%i %p\n", __func__, session->serial, session);
}

void
session_clear(MinnetSession* session, JSContext* ctx) {
  // list_del(&session->link);

  JS_FreeValue(ctx, session->ws_obj);
  JS_FreeValue(ctx, session->req_obj);
  JS_FreeValue(ctx, session->resp_obj);
  JS_FreeValue(ctx, session->generator);
  JS_FreeValue(ctx, session->next);

  buffer_free(&session->send_buf, JS_GetRuntime(ctx));

  // printf("%s #%i %p\n", __func__, session->serial, session);
}

struct http_response*
session_response(MinnetSession* session, MinnetCallback* cb) {
  MinnetResponse* resp = minnet_response_data2(cb->ctx, session->resp_obj);

  if(cb && cb->ctx) {
    JSValue ret = minnet_emit_this(cb, session->ws_obj, 2, session->args);
    lwsl_user("session_response ret=%s", JS_ToCString(cb->ctx, ret));
    if(JS_IsObject(ret) && minnet_response_data2(cb->ctx, ret)) {
      JS_FreeValue(cb->ctx, session->args[1]);
      session->args[1] = ret;
      resp = minnet_response_data2(cb->ctx, ret);
    } else {
      JS_FreeValue(cb->ctx, ret);
    }
  }
  lwsl_user("session_response %s", response_dump(resp));

  return resp;
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

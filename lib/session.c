#include "session.h"
#include "opaque.h"

static THREAD_LOCAL uint32_t session_serial = 0;
THREAD_LOCAL struct list_head session_list = {0, 0};

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
}

void
session_add(MinnetSession* session) {
  if(session_list.prev == NULL)
    init_list_head(&session_list);

  list_add(&session->link, &session_list);
}

void
session_remove(MinnetSession* session) {
  list_del(&session->link);
}

void
session_clear(MinnetSession* session, JSContext* ctx) {

  JS_FreeValue(ctx, session->ws_obj);
  JS_FreeValue(ctx, session->req_obj);
  JS_FreeValue(ctx, session->resp_obj);
  JS_FreeValue(ctx, session->generator);
  JS_FreeValue(ctx, session->next);

  buffer_free(&session->send_buf, JS_GetRuntime(ctx));

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

JSValue
minnet_get_sessions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct list_head* el;
  JSValue ret;
  uint32_t i = 0;

  ret = JS_NewArray(ctx);

  list_for_each(el, &session_list) {
    struct wsi_opaque_user_data* session = list_entry(el, struct wsi_opaque_user_data, link);
    // printf("%s @%u #%i %p\n", __func__, i, session->serial, session);

    JS_SetPropertyUint32(ctx, ret, i++, session_object(session, ctx));
  }
  return ret;
}

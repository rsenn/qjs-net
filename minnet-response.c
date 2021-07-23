#include "minnet.h"
#include "minnet-websocket.h"
#include "minnet-response.h"

JSClassID minnet_response_class_id;

JSValue
minnet_response_buffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res && res->buffer) {
    JSValue val = JS_NewArrayBufferCopy(ctx, res->buffer, res->size);
    return val;
  }

  return JS_EXCEPTION;
}

JSValue
minnet_response_json(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res && res->buffer)
    return JS_ParseJSON(ctx, (char*)res->buffer, res->size, "<input>");

  return JS_EXCEPTION;
}

JSValue
minnet_response_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res && res->buffer)
    return JS_NewStringLen(ctx, (char*)res->buffer, res->size);

  return JS_EXCEPTION;
}

JSValue
minnet_response_getter_ok(JSContext* ctx, JSValueConst this_val) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return res->ok;

  return JS_EXCEPTION;
}

JSValue
minnet_response_getter_url(JSContext* ctx, JSValueConst this_val) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return res->url;

  return JS_EXCEPTION;
}

JSValue
minnet_response_getter_status(JSContext* ctx, JSValueConst this_val) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return res->status;

  return JS_EXCEPTION;
}

JSValue
minnet_response_getter_type(JSContext* ctx, JSValueConst this_val) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res) {
    return res->type;
  }

  return JS_EXCEPTION;
}

void
minnet_response_finalizer(JSRuntime* rt, JSValue val) {
  MinnetResponse* res = JS_GetOpaque(val, minnet_response_class_id);
  if(res) {
    if(res->buffer)
      free(res->buffer);
    js_free_rt(rt, res);
  }
}

JSClassDef minnet_response_class = {
    "MinnetResponse",
    .finalizer = minnet_response_finalizer,
};

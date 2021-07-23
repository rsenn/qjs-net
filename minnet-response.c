#include "minnet.h"
#include "minnet-websocket.h"
#include "minnet-response.h"

JSClassID minnet_response_class_id;
JSValue minnet_response_proto;

void
minnet_response_dump(JSContext* ctx, struct http_response* res) {
  printf("{");
  înt32_t status;
  BOOL ok;
  char *url, *type;
  JS_ToInt32(ctx, &status, res->status);
  ok = JS_ToBool(ctx, res->ok);
  url = JS_ToCString(ctx, res->url);
  type = JS_ToCString(ctx, res->type);
  printf(" status = %d, ok = %d, url = %s, type = %s", status, ok, url, type);
  printf(" }\n");
  /*
    value_dump(ctx, "status", &res->status);
    value_dump(ctx, "ok", &res->ok);
    value_dump(ctx, "url", &res->url);
    value_dump(ctx, "type", &res->type);
    printf("\n}\n");*/
  fflush(stdout);
}
void
minnet_response_zero(struct http_response* res) {
  res->buffer = 0;
  res->size = 0;
  res->status = res->ok = res->url = res->type = JS_UNDEFINED;
}
void
minnet_response_init(JSContext* ctx, MinnetResponse* res, int32_t status, BOOL ok, const char* url, const char* type) {
  res->status = JS_NewInt32(ctx, status);
  res->ok = JS_NewBool(ctx, ok);
  res->url = JS_NewString(ctx, url);
  res->type = JS_NewString(ctx, type);
  res->buffer = 0;
  res->size = 0;
}

void
minnet_response_free(JSRuntime* rt, MinnetResponse* res) {
  JS_FreeValueRT(rt, res->status);
  res->status = JS_UNDEFINED;
  JS_FreeValueRT(rt, res->ok);
  res->ok = JS_UNDEFINED;
  JS_FreeValueRT(rt, res->url);
  res->url = JS_UNDEFINED;
  JS_FreeValueRT(rt, res->type);
  res->type = JS_UNDEFINED;

  /*  res->buffer = 0;
    res->size = 0;*/
}

JSValue
minnet_response_new(JSContext* ctx, int32_t status, BOOL ok, const char* url, const char* type, uint8_t* buf, size_t len) {
  MinnetResponse* res = js_mallocz(ctx, sizeof(MinnetResponse));

  minnet_response_init(ctx, res, status, ok, url, type);

  if(buf) {
    res->buffer = js_malloc(ctx, (res->size = len));
    memcpy(res->buffer, buf, len);
  }
  return minnet_response_wrap(ctx, res);
}

JSValue
minnet_response_wrap(JSContext* ctx, MinnetResponse* res) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_response_proto, minnet_response_class_id);
  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, res);
  return ret;
}

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

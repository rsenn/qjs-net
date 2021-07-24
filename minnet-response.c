#include "minnet.h"
#include "minnet-websocket.h"
#include "minnet-response.h"

JSClassID minnet_response_class_id;
JSValue minnet_response_proto;

static void
body_dump(const char* n, struct http_body const* b) {
  printf("\n\t%s\t{ times = %zx, budget = %zx }", n, b->times, b->budget);
  fflush(stdout);
}

void
minnet_response_dump(JSContext* ctx, struct http_response const* res) {
  printf("{");
  int32_t status = -1, ok = -1;
  const char *url = "nil", *type = "nil";
  JS_ToInt32(ctx, &status, res->status);
  ok = JS_ToBool(ctx, res->ok);
  if(JS_VALUE_GET_TAG(res->url))
    url = JS_ToCString(ctx, res->url);
  if(JS_IsString(res->type))
    type = JS_ToCString(ctx, res->type);
  printf(" status = %d, ok = %d, url = %s, type = %s", status, ok, url, type);
  body_dump("state", &res->state);
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
  res->buffer = (MinnetBuffer){.start = 0, .pos = 0, .end = 0};
  res->status = res->ok = res->url = res->type = JS_UNDEFINED;
}
void
minnet_response_init(JSContext* ctx, MinnetResponse* res, int32_t status, BOOL ok, const char* url, const char* type) {
  res->status = JS_NewInt32(ctx, status);
  res->ok = JS_NewBool(ctx, ok);
  res->url = url ? JS_NewString(ctx, url) : JS_UNDEFINED;
  res->type = type ? JS_NewString(ctx, type) : JS_UNDEFINED;
  res->buffer = (MinnetBuffer){.start = 0, .pos = 0, .end = 0};
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

  if(buf)
    buffer_init(&res->buffer, buf, len);
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
  if(res) {
    JSValue val = JS_NewArrayBufferCopy(ctx, res->buffer.start, res->buffer.pos - res->buffer.start);
    return val;
  }

  return JS_EXCEPTION;
}

JSValue
minnet_response_json(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return JS_ParseJSON(ctx, (char*)res->buffer.start, res->buffer.pos - res->buffer.start, "<input>");

  return JS_EXCEPTION;
}

JSValue
minnet_response_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return JS_NewStringLen(ctx, (char*)res->buffer.start, res->buffer.pos - res->buffer.start);

  return JS_EXCEPTION;
}

JSValue
minnet_response_getter_ok(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return res->ok;

  return JS_EXCEPTION;
}

JSValue
minnet_response_getter_url(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return res->url;

  return JS_EXCEPTION;
}

JSValue
minnet_response_getter_status(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return res->status;

  return JS_EXCEPTION;
}

JSValue
minnet_response_getter_type(JSContext* ctx, JSValueConst this_val, int magic) {
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
    if(res->buffer.start)
      free(res->buffer.start - LWS_PRE);
    js_free_rt(rt, res);
  }
}

JSClassDef minnet_response_class = {
    "MinnetResponse",
    .finalizer = minnet_response_finalizer,
};

const JSCFunctionListEntry minnet_response_proto_funcs[] = {
    JS_CFUNC_DEF("arrayBuffer", 0, minnet_response_buffer),
    JS_CFUNC_DEF("json", 0, minnet_response_json),
    JS_CFUNC_DEF("text", 0, minnet_response_text),
    JS_CGETSET_FLAGS_DEF("ok", minnet_response_getter_ok, NULL, JS_PROP_ENUMERABLE),
    JS_CGETSET_FLAGS_DEF("url", minnet_response_getter_url, NULL, JS_PROP_ENUMERABLE),
    JS_CGETSET_FLAGS_DEF("status", minnet_response_getter_status, NULL, JS_PROP_ENUMERABLE),
    JS_CGETSET_FLAGS_DEF("type", minnet_response_getter_type, NULL, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetResponse", JS_PROP_CONFIGURABLE),
};

const size_t minnet_response_proto_funcs_size = countof(minnet_response_proto_funcs);

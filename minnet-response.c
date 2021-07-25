#include "minnet.h"
#include "minnet-websocket.h"
#include "minnet-response.h"
#include "buffer.h"
#include <cutils.h>

JSClassID minnet_response_class_id;
JSValue minnet_response_proto;

enum { RESPONSE_OK, RESPONSE_URL, RESPONSE_STATUS, RESPONSE_TYPE, RESPONSE_LENGTH };

static void
body_dump(const char* n, struct http_state const* b) {
  printf("\n\t%s\t{ times = %zx, budget = %zx }", n, b->times, b->budget);
  fflush(stdout);
}

void
minnet_response_dump(JSContext* ctx, struct http_response const* res) {
  printf("{");
  printf(" status = %d, ok = %d, url = %s, type = %s", res->status, res->ok, res->url, res->type);
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
  memset(res, 0, sizeof(MinnetResponse));
  res->body = (MinnetBuffer){.start = 0, .pos = 0, .end = 0};
}

void
minnet_response_init(JSContext* ctx, MinnetResponse* res, int32_t status, BOOL ok, const char* url, const char* type) {
  memset(res, 0, sizeof(MinnetResponse));

  res->status = status;
  res->ok = ok;
  res->url = url ? js_strdup(ctx, url) : 0;
  res->type = type ? js_strdup(ctx, type) : 0;
  res->body = (MinnetBuffer){.start = 0, .pos = 0, .end = 0};
}

void
minnet_response_free(JSRuntime* rt, MinnetResponse* res) {
  js_free_rt(rt, res->url);
  res->url = 0;
  js_free_rt(rt, res->type);
  res->type = 0;

  buffer_free(&res->body, rt);
}

MinnetResponse*
minnet_response_new(JSContext* ctx, int32_t status, BOOL ok, const char* url, const char* type) {
  MinnetResponse* res = js_mallocz(ctx, sizeof(MinnetResponse));

  minnet_response_init(ctx, res, status, ok, url, type);

  return res;
}

JSValue
minnet_response_wrap(JSContext* ctx, MinnetResponse* res) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_response_proto, minnet_response_class_id);
  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, res);
  return ret;
}

static JSValue
minnet_response_buffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res) {
    JSValue val = JS_NewArrayBufferCopy(ctx, res->body.start, res->body.pos - res->body.start);
    return val;
  }

  return JS_EXCEPTION;
}

static JSValue
minnet_response_json(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return JS_ParseJSON(ctx, (char*)res->body.start, res->body.pos - res->body.start, "<input>");

  return JS_EXCEPTION;
}

static JSValue
minnet_response_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return JS_NewStringLen(ctx, (char*)res->body.start, res->body.pos - res->body.start);

  return JS_EXCEPTION;
}

static JSValue
minnet_response_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetResponse* res;
  JSValue ret = JS_UNDEFINED;
  if(!(res = JS_GetOpaque2(ctx, this_val, minnet_response_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case RESPONSE_STATUS: {
      ret = JS_NewInt32(ctx, res->status);
      break;
    }
    case RESPONSE_OK: {
      ret = JS_NewBool(ctx, res->ok);
      break;
    }
    case RESPONSE_URL: {
      ret = res->url ? JS_NewString(ctx, res->url) : JS_NULL;
      break;
    }
    case RESPONSE_TYPE: {
      ret = res->type ? JS_NewString(ctx, res->type) : JS_NULL;
      break;
    }
    case RESPONSE_LENGTH: {
      ret = JS_NewInt64(ctx, buffer_SIZE(&res->body));
      break;
    }
  }

  return ret;
}

void
minnet_response_finalizer(JSRuntime* rt, JSValue val) {
  MinnetResponse* res = JS_GetOpaque(val, minnet_response_class_id);
  if(res) {
    if(res->body.start)
      free(res->body.start - LWS_PRE);
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
    JS_CGETSET_MAGIC_FLAGS_DEF("ok", minnet_response_get, 0, RESPONSE_OK, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("url", minnet_response_get, 0, RESPONSE_URL, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("status", minnet_response_get, 0, RESPONSE_STATUS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_response_get, 0, RESPONSE_TYPE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("byteLength", minnet_response_get, 0, RESPONSE_LENGTH, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetResponse", JS_PROP_CONFIGURABLE),
};

const size_t minnet_response_proto_funcs_size = countof(minnet_response_proto_funcs);

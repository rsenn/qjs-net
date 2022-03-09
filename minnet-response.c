#include "minnet-websocket.h"
#include "minnet-response.h"
#include "minnet-buffer.h"
#include "jsutils.h"
#include <cutils.h>

THREAD_LOCAL JSClassID minnet_response_class_id;
THREAD_LOCAL JSValue minnet_response_proto, minnet_response_ctor;

enum { RESPONSE_BUFFER, RESPONSE_JSON, RESPONSE_TEXT, RESPONSE_HEADER };
enum { RESPONSE_OK, RESPONSE_URL, RESPONSE_STATUS, RESPONSE_TYPE, RESPONSE_OFFSET, RESPONSE_HEADERS };

/*struct http_header*
header_new(JSContext* ctx, const char* name, const char* value) {
  MinnetHttpHeader* hdr;

  if(!(hdr = js_mallocz(ctx, sizeof(MinnetHttpHeader))))
    JS_ThrowOutOfMemory(ctx);

  hdr->name = js_strdup(ctx, name);
  hdr->value = js_strdup(ctx, value);

  return hdr;
}

void
header_free(JSRuntime* rt, struct http_header* hdr) {
  js_free_rt(rt, hdr->name);
  js_free_rt(rt, hdr->value);
  js_free_rt(rt, hdr);
}*/

void
response_format(struct http_response const* res, char* buf, size_t len) {
  snprintf(buf, len, FGC(226, "MinnetResponse") " { url: '%s', status: %d, ok: %s, type: '%s' }", res->url, res->status, res->ok ? "true" : "false", res->type);
}

char*
response_dump(struct http_response const* res) {
  static char buf[1024];
  response_format(res, buf, sizeof(buf));
  return buf;
}

void
response_zero(struct http_response* res) {
  memset(res, 0, sizeof(MinnetResponse));
  res->body = BUFFER_0();
}

void
response_init(struct http_response* res, MinnetURL url, int32_t status, BOOL ok, char* type) {
  memset(res, 0, sizeof(MinnetResponse));

  res->status = status;
  res->ok = ok;
  res->url = url;
  res->type = type;
  res->headers = BUFFER_0();
  res->body = BUFFER_0();
}

ssize_t
response_write(struct http_response* res, const void* x, size_t n, JSContext* ctx) {
  return buffer_append(&res->body, x, n, ctx);
}

void
response_free(JSRuntime* rt, struct http_response* res) {
  url_free_rt(&res->url, rt);
  js_free_rt(rt, (void*)res->type);
  res->type = 0;

  buffer_free(&res->headers, rt);
  buffer_free(&res->body, rt);

  js_free_rt(rt, res);
}

struct http_response*
response_new(JSContext* ctx) {
  MinnetResponse* res;

  if(!(res = js_mallocz(ctx, sizeof(MinnetResponse))))
    JS_ThrowOutOfMemory(ctx);

  return res;
}

JSValue
minnet_response_new(JSContext* ctx, MinnetURL url, int32_t status, BOOL ok, const char* type) {
  MinnetResponse* res;

  if((res = response_new(ctx))) {
    response_init(res, url, status, ok, type ? js_strdup(ctx, type) : 0);

    return minnet_response_wrap(ctx, res);
  }

  return JS_NULL;
}

JSValue
minnet_response_wrap(JSContext* ctx, struct http_response* res) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_response_proto, minnet_response_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, res);
  return ret;
}

static JSValue
minnet_response_buffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetResponse* res;

  if((res = JS_GetOpaque2(ctx, this_val, minnet_response_class_id))) {
    JSValue val = JS_NewArrayBuffer /*Copy*/ (ctx, block_BEGIN(&res->body), block_SIZE(&res->body), 0, 0, 0);
    return val;
  }

  return JS_EXCEPTION;
}

static JSValue
minnet_response_clone(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetResponse *res, *clone;

  if(!(res = minnet_response_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(clone = response_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  clone->read_only = res->read_only;
  clone->status = res->status;
  clone->read_only = res->read_only;
  clone->url = url_dup(res->url, ctx);
  clone->type = js_strdup(ctx, res->type);

  buffer_clone(&clone->headers, &res->headers, ctx);
  buffer_clone(&clone->body, &res->body, ctx);

  return minnet_response_wrap(ctx, clone);
}

static JSValue
minnet_response_json(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetResponse* res;
  if((res = JS_GetOpaque2(ctx, this_val, minnet_response_class_id)))
    return JS_ParseJSON(ctx, block_BEGIN(&res->body), buffer_HEAD(&res->body), res->url.path);

  return JS_EXCEPTION;
}

static JSValue
minnet_response_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetResponse* res;
  if((res = JS_GetOpaque2(ctx, this_val, minnet_response_class_id)))
    return JS_NewStringLen(ctx, (char*)block_BEGIN(&res->body), buffer_HEAD(&res->body));

  return JS_EXCEPTION;
}

/*static JSValue
minnet_response_header(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetResponse* res;
  MinnetHttpHeader* hdr;
  const char *name, *value;
  JSValue ret = JS_FALSE;

  if(!(res = JS_GetOpaque2(ctx, this_val, minnet_response_class_id)))
    return JS_EXCEPTION;

  name = JS_ToCString(ctx, argv[0]);
  value = JS_ToCString(ctx, argv[1]);

  if((hdr = header_new(ctx, name, value))) {
    if(!res->headers.next)
      init_list_head(&res->headers);

    list_add_tail(&hdr->link, &res->headers);
    ret = JS_TRUE;
  }

  JS_FreeCString(ctx, name);
  JS_FreeCString(ctx, value);

  return ret;
}*/

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
      ret = minnet_url_new(ctx, res->url);
      break;
    }
    case RESPONSE_TYPE: {
      ret = res->type ? JS_NewString(ctx, res->type) : JS_NULL;
      break;
    }
    case RESPONSE_OFFSET: {
      ret = JS_NewInt64(ctx, buffer_HEAD(&res->body));
      break;
    }
    case RESPONSE_HEADERS: {
      ret = headers_object(ctx, res->headers.start, res->headers.end);
      break;
    }
  }

  return ret;
}

static JSValue
minnet_response_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetResponse* resp;
  JSValue ret = JS_UNDEFINED;
  const char* str;
  size_t len;
  if(!(resp = JS_GetOpaque2(ctx, this_val, minnet_response_class_id)))
    return JS_EXCEPTION;

  if(resp->read_only)
    return JS_ThrowReferenceError(ctx, "Response object is read-only");

  str = JS_ToCStringLen(ctx, &len, value);

  switch(magic) {
    case RESPONSE_STATUS: {
      int32_t s;
      if(!JS_ToInt32(ctx, &s, value))
        resp->status = s;
      break;
    }
    case RESPONSE_OK: {
      resp->ok = JS_ToBool(ctx, value);
      break;
    }
    case RESPONSE_URL: {
      url_free(&resp->url, ctx);
      url_parse(&resp->url, str, ctx);
      break;
    }
    case RESPONSE_TYPE: {
      resp->type = js_strdup(ctx, str);
      break;
    }
    case RESPONSE_OFFSET: {
      uint64_t o;
      if(!JS_ToIndex(ctx, &o, value))
        resp->body.write = resp->body.start + o;
      break;
    }
  }
  JS_FreeCString(ctx, str);
  return ret;
}

JSValue
minnet_response_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetResponse* resp;
  int i;

  if(!(resp = js_mallocz(ctx, sizeof(MinnetResponse))))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_response_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_response_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  if(argc >= 1 && argc < 3) {

    if(!js_is_nullish(argv[0])) {
      buffer_fromvalue(&resp->body, argv[0], ctx);
    }

    argc--;
    argv++;
  }

  for(i = 0; i < argc; i++) {
    if(JS_IsObject(argv[i]) && !JS_IsNull(argv[i])) {
      js_copy_properties(ctx, obj, argv[i], JS_GPN_STRING_MASK);
      argc--;
      argv++;
    } else if(JS_IsString(argv[i])) {
      const char* str = JS_ToCString(ctx, argv[i]);
      if(!resp->url.path)
        url_parse(&resp->url, str, ctx);
      else if(!resp->type)
        resp->type = js_strdup(ctx, str);
      JS_FreeCString(ctx, str);

    } else if(JS_IsBool(argv[i])) {
      resp->ok = JS_ToBool(ctx, argv[i]);
    } else if(JS_IsNumber(argv[i])) {
      int32_t s;
      if(!JS_ToInt32(ctx, &s, argv[i]))
        resp->status = s;
    }
  }

  JS_SetOpaque(obj, resp);

  return obj;

fail:
  js_free(ctx, resp);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

void
minnet_response_finalizer(JSRuntime* rt, JSValue val) {
  MinnetResponse* res = JS_GetOpaque(val, minnet_response_class_id);
  if(res) {
    buffer_free(&res->headers, rt);
    buffer_free(&res->body, rt);

    url_free_rt(&res->url, rt);
    if(res->type)
      js_free_rt(rt, res->type);

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
    JS_CFUNC_DEF("clone", 0, minnet_response_clone),
    // JS_CFUNC_DEF("header", 2, minnet_response_header),
    JS_CGETSET_MAGIC_FLAGS_DEF("status", minnet_response_get, minnet_response_set, RESPONSE_STATUS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("ok", minnet_response_get, minnet_response_set, RESPONSE_OK, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("url", minnet_response_get, minnet_response_set, RESPONSE_URL, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_response_get, minnet_response_set, RESPONSE_TYPE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("offset", minnet_response_get, minnet_response_set, RESPONSE_OFFSET),
    JS_CGETSET_MAGIC_FLAGS_DEF("headers", minnet_response_get, 0, RESPONSE_HEADERS, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetResponse", JS_PROP_CONFIGURABLE),
};

const size_t minnet_response_proto_funcs_size = countof(minnet_response_proto_funcs);

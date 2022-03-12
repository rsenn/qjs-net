#include "minnet-websocket.h"
#include "minnet-response.h"
#include "minnet-buffer.h"
#include "jsutils.h"
#include <cutils.h>

THREAD_LOCAL JSClassID minnet_response_class_id;
THREAD_LOCAL JSValue minnet_response_proto, minnet_response_ctor;

enum { RESPONSE_HEADER };
enum { RESPONSE_OK, RESPONSE_URL, RESPONSE_STATUS, RESPONSE_STATUSTEXT, RESPONSE_REDIRECTED, RESPONSE_BODYUSED, RESPONSE_TYPE, RESPONSE_OFFSET, RESPONSE_HEADERS };

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
response_format(MinnetResponse const* resp, char* buf, size_t len) {
  snprintf(buf, len, FGC(226, "MinnetResponse") " { url: '%s', status: %d, ok: %s, type: '%s' }", resp->url, resp->status, resp->ok ? "true" : "false", resp->type);
}

char*
response_dump(MinnetResponse const* resp) {
  static char buf[1024];
  response_format(resp, buf, sizeof(buf));
  return buf;
}

void
response_zero(MinnetResponse* resp) {
  memset(resp, 0, sizeof(MinnetResponse));
  resp->body = BUFFER_0();
}

void
response_init(MinnetResponse* resp, MinnetURL url, int32_t status, char* status_text, BOOL ok, char* type) {
  memset(resp, 0, sizeof(MinnetResponse));

  resp->status = status;
  resp->status_text = status_text;
  resp->ok = ok;
  resp->url = url;
  resp->type = type;
  resp->headers = BUFFER_0();
  resp->body = BUFFER_0();
}

MinnetResponse*
response_dup(MinnetResponse* resp) {
  ++resp->ref_count;
  return resp;
}

ssize_t
response_write(MinnetResponse* resp, const void* x, size_t n, JSContext* ctx) {
  return buffer_append(&resp->body, x, n, ctx);
}
void
response_clear(MinnetResponse* resp, JSContext* ctx) {
  url_free(&resp->url, ctx);
  if(resp->type) {
    js_free(ctx, (void*)resp->type);
    resp->type = 0;
  }

  buffer_free(&resp->headers, JS_GetRuntime(ctx));
  buffer_free(&resp->body, JS_GetRuntime(ctx));
}

void
response_clear_rt(MinnetResponse* resp, JSRuntime* rt) {
  url_free_rt(&resp->url, rt);
  if(resp->type) {
    js_free_rt(rt, (void*)resp->type);
    resp->type = 0;
  }

  buffer_free(&resp->headers, rt);
  buffer_free(&resp->body, rt);
}

void
response_free(MinnetResponse* resp, JSContext* ctx) {

  if(--resp->ref_count == 0) {
    response_clear(resp, ctx);
    js_free(ctx, resp);
  }
}

void
response_free_rt(MinnetResponse* resp, JSRuntime* rt) {
  if(--resp->ref_count == 0) {
    response_clear_rt(resp, rt);
    js_free_rt(rt, resp);
  }
}

MinnetResponse*
response_new(JSContext* ctx) {
  MinnetResponse* resp;

  if(!(resp = js_mallocz(ctx, sizeof(MinnetResponse))))
    JS_ThrowOutOfMemory(ctx);

  return resp;
}

JSValue
minnet_response_new(JSContext* ctx, MinnetURL url, int status, char* status_text, BOOL ok, const char* type) {
  MinnetResponse* resp;

  if((resp = response_new(ctx))) {
    response_init(resp, url, status, status_text, ok, type ? js_strdup(ctx, type) : 0);

    return minnet_response_wrap(ctx, resp);
  }

  return JS_NULL;
}

JSValue
minnet_response_wrap(JSContext* ctx, MinnetResponse* resp) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_response_proto, minnet_response_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, resp);
  return ret;
}

enum {
  RESPONSE_ARRAYBUFFER = 0,
  RESPONSE_TEXT,
  RESPONSE_JSON,
};

static JSValue
minnet_response_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret, result = JS_UNDEFINED;
  ResolveFunctions funcs;
  MinnetResponse* resp;

  if(!(resp = minnet_response_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case RESPONSE_ARRAYBUFFER: {
      result = JS_NewArrayBuffer /*Copy*/ (ctx, block_BEGIN(&resp->body), block_SIZE(&resp->body), 0, 0, 0);
      break;
    }
    case RESPONSE_TEXT: {
      result = JS_NewStringLen(ctx, (char*)block_BEGIN(&resp->body), buffer_HEAD(&resp->body));
      break;
    }
    case RESPONSE_JSON: {
      result = JS_ParseJSON(ctx, block_BEGIN(&resp->body), buffer_HEAD(&resp->body), resp->url.path);
      break;
    }
  }

  ret = js_promise_create(ctx, &funcs);

  js_promise_resolve(ctx, &funcs, result);
  JS_FreeValue(ctx, result);

  return ret;
}

/*static JSValue
minnet_response_buffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetResponse* resp;
  if((resp = minnet_response_data2(ctx, this_val))) {
    JSValue val = JS_NewArrayBuffer(ctx, block_BEGIN(&resp->body), block_SIZE(&resp->body), 0, 0, 0);
    return val;
  }
  return JS_EXCEPTION;
}

static JSValue
minnet_response_json(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetResponse* resp;
  if((resp = minnet_response_data2(ctx, this_val)))
    return JS_ParseJSON(ctx, block_BEGIN(&resp->body), buffer_HEAD(&resp->body), resp->url.path);
  return JS_EXCEPTION;
}

static JSValue
minnet_response_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetResponse* resp;
  if((resp = minnet_response_data2(ctx, this_val)))
    return JS_NewStringLen(ctx, (char*)block_BEGIN(&resp->body), buffer_HEAD(&resp->body));
  return JS_EXCEPTION;
}*/

static JSValue
minnet_response_clone(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetResponse *resp, *clone;

  if(!(resp = minnet_response_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(clone = response_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  clone->read_only = resp->read_only;
  clone->status = resp->status;
  clone->read_only = resp->read_only;
  clone->url = url_clone(resp->url, ctx);
  clone->type = js_strdup(ctx, resp->type);

  buffer_clone(&clone->headers, &resp->headers, ctx);
  buffer_clone(&clone->body, &resp->body, ctx);

  return minnet_response_wrap(ctx, clone);
}

/*static JSValue
minnet_response_header(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetResponse* resp;
  MinnetHttpHeader* hdr;
  const char *name, *value;
  JSValue ret = JS_FALSE;

  if(!(resp = minnet_response_data2(ctx, this_val)))
    return JS_EXCEPTION;

  name = JS_ToCString(ctx, argv[0]);
  value = JS_ToCString(ctx, argv[1]);

  if((hdr = header_new(ctx, name, value))) {
    if(!resp->headers.next)
      init_list_head(&resp->headers);

    list_add_tail(&hdr->link, &resp->headers);
    ret = JS_TRUE;
  }

  JS_FreeCString(ctx, name);
  JS_FreeCString(ctx, value);

  return ret;
}*/

static JSValue
minnet_response_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetResponse* resp;
  JSValue ret = JS_UNDEFINED;
  if(!(resp = minnet_response_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case RESPONSE_STATUS: {
      ret = JS_NewInt32(ctx, resp->status);
      break;
    }
    case RESPONSE_STATUSTEXT: {
      ret = resp->status_text ? JS_NewString(ctx, resp->status_text) : JS_NULL;
      break;
    }
    case RESPONSE_OK: {
      ret = JS_NewBool(ctx, resp->ok);
      break;
    }
    case RESPONSE_URL: {
      ret = minnet_url_new(ctx, resp->url);
      break;
    }
    case RESPONSE_TYPE: {
      ret = resp->type ? JS_NewString(ctx, resp->type) : JS_NULL;
      break;
    }
      /* case RESPONSE_OFFSET: {
         ret = JS_NewInt64(ctx, buffer_HEAD(&resp->body));
         break;
       }*/
    case RESPONSE_HEADERS: {
      ret = headers_object(ctx, resp->headers.start, resp->headers.end);
      break;
    }
    case RESPONSE_BODYUSED: {
      ret = JS_NewBool(ctx, buffer_SIZE(&resp->body) > 0);
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
  if(!(resp = minnet_response_data2(ctx, this_val)))
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
      /* case RESPONSE_OFFSET: {
         uint64_t o;
         if(!JS_ToIndex(ctx, &o, value))
           resp->body.write = resp->body.start + o;
         break;
       }*/
    case RESPONSE_BODYUSED: {
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
  MinnetResponse* res;

  if((res = minnet_response_data(val)))
    response_free_rt(res, rt);
}

JSClassDef minnet_response_class = {
    "MinnetResponse",
    .finalizer = minnet_response_finalizer,
};

const JSCFunctionListEntry minnet_response_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("arrayBuffer", 0, minnet_response_method, RESPONSE_ARRAYBUFFER),
    JS_CFUNC_MAGIC_DEF("text", 0, minnet_response_method, RESPONSE_TEXT),
    JS_CFUNC_MAGIC_DEF("json", 0, minnet_response_method, RESPONSE_JSON),
    JS_CGETSET_MAGIC_FLAGS_DEF("status", minnet_response_get, minnet_response_set, RESPONSE_STATUS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("statusText", minnet_response_get, minnet_response_set, RESPONSE_STATUSTEXT, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("bodyUsed", minnet_response_get, 0, RESPONSE_BODYUSED, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("ok", minnet_response_get, minnet_response_set, RESPONSE_OK, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("redirected", minnet_response_get, minnet_response_set, RESPONSE_REDIRECTED, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("url", minnet_response_get, minnet_response_set, RESPONSE_URL, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_response_get, minnet_response_set, RESPONSE_TYPE, JS_PROP_ENUMERABLE),
    //  JS_CGETSET_MAGIC_DEF("offset", minnet_response_get, minnet_response_set, RESPONSE_OFFSET),
    JS_CGETSET_MAGIC_FLAGS_DEF("headers", minnet_response_get, 0, RESPONSE_HEADERS, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetResponse", JS_PROP_CONFIGURABLE),
};

const size_t minnet_response_proto_funcs_size = countof(minnet_response_proto_funcs);

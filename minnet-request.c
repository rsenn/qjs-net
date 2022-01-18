#define _GNU_SOURCE
#include "minnet-request.h"
#define _GNU_SOURCE
#include "minnet-stream.h"
#define _GNU_SOURCE
#include "jsutils.h"
#define _GNU_SOURCE
#include <cutils.h>
#define _GNU_SOURCE
#include <ctype.h>
#define _GNU_SOURCE

#define _GNU_SOURCE
THREAD_LOCAL JSClassID minnet_request_class_id;
#define _GNU_SOURCE
THREAD_LOCAL JSValue minnet_request_proto, minnet_request_ctor;
#define _GNU_SOURCE

#define _GNU_SOURCE
enum { REQUEST_TYPE, REQUEST_METHOD, REQUEST_URI, REQUEST_PATH, REQUEST_HEADERS, REQUEST_ARRAYBUFFER, REQUEST_TEXT, REQUEST_BODY };
#define _GNU_SOURCE

#define _GNU_SOURCE
static const char* const method_names[] = {"GET", "POST", "OPTIONS", "PUT", "PATCH", "DELETE", "CONNECT", "HEAD"};
#define _GNU_SOURCE

#define _GNU_SOURCE
static const char*
#define _GNU_SOURCE
method2name(enum http_method m) {
#define _GNU_SOURCE
  if(m >= 0 && m < countof(method_names))
#define _GNU_SOURCE
    return method_names[m];
#define _GNU_SOURCE
  return 0;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
static enum http_method
#define _GNU_SOURCE
name2method(const char* name) {
#define _GNU_SOURCE
  unsigned long i;
#define _GNU_SOURCE
  for(i = 0; i < countof(method_names); i++) {
#define _GNU_SOURCE
    if(!strcasecmp(name, method_names[i]))
#define _GNU_SOURCE
      return i;
#define _GNU_SOURCE
  }
#define _GNU_SOURCE
  return -1;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
void
#define _GNU_SOURCE
request_format(struct http_request const* req, char* buf, size_t len, JSContext* ctx) {
#define _GNU_SOURCE
  char* headers = buffer_escaped(&req->headers, ctx);
#define _GNU_SOURCE

#define _GNU_SOURCE
  snprintf(buf, len, FGC(196, "MinnetRequest") " { method: '%s', url: '%s', path: '%s', headers: '%s' }", method_name(req->method), req->url, req->path, headers);
#define _GNU_SOURCE

#define _GNU_SOURCE
  js_free(ctx, headers);
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
char*
#define _GNU_SOURCE
request_dump(struct http_request const* req, JSContext* ctx) {
#define _GNU_SOURCE
  static char buf[2048];
#define _GNU_SOURCE
  request_format(req, buf, sizeof(buf), ctx);
#define _GNU_SOURCE
  return buf;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
void
#define _GNU_SOURCE
request_init(struct http_request* req, const char* path, char* url, MinnetHttpMethod method) {
#define _GNU_SOURCE
  memset(req, 0, sizeof(*req));
#define _GNU_SOURCE

#define _GNU_SOURCE
  req->ref_count = 0;
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(path)
#define _GNU_SOURCE
    pstrcpy(req->path, sizeof(req->path), path);
#define _GNU_SOURCE

#define _GNU_SOURCE
  req->url = url;
#define _GNU_SOURCE
  req->method = method;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
struct http_request*
#define _GNU_SOURCE
request_new(JSContext* ctx, const char* path, char* url, MinnetHttpMethod method) {
#define _GNU_SOURCE
  MinnetRequest* req;
#define _GNU_SOURCE

#define _GNU_SOURCE
  if((req = js_mallocz(ctx, sizeof(MinnetRequest))))
#define _GNU_SOURCE
    request_init(req, path, url, method);
#define _GNU_SOURCE

#define _GNU_SOURCE
  return req;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
void
#define _GNU_SOURCE
request_zero(struct http_request* req) {
#define _GNU_SOURCE
  memset(req, 0, sizeof(MinnetRequest));
#define _GNU_SOURCE
  req->headers = BUFFER_0();
#define _GNU_SOURCE
  req->body = BUFFER_0();
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
static const char*
#define _GNU_SOURCE
header_get(JSContext* ctx, size_t* lenp, struct byte_buffer* buf, const char* name) {
#define _GNU_SOURCE
  size_t len, namelen = strlen(name);
#define _GNU_SOURCE
  uint8_t *x, *end;
#define _GNU_SOURCE

#define _GNU_SOURCE
  for(x = buf->start, end = buf->write; x < end; x += len + 1) {
#define _GNU_SOURCE
    len = byte_chr(x, end - x, '\n');
#define _GNU_SOURCE

#define _GNU_SOURCE
    /*   if(namelen >= len)
#define _GNU_SOURCE
         continue;*/
#define _GNU_SOURCE
    if(byte_chr(x, len, ':') != namelen || strncasecmp(name, (const char*)x, namelen))
#define _GNU_SOURCE
      continue;
#define _GNU_SOURCE

#define _GNU_SOURCE
    if(x[namelen] == ':')
#define _GNU_SOURCE
      namelen++;
#define _GNU_SOURCE
    if(isspace(x[namelen]))
#define _GNU_SOURCE
      namelen++;
#define _GNU_SOURCE

#define _GNU_SOURCE
    if(lenp)
#define _GNU_SOURCE
      *lenp = len - namelen;
#define _GNU_SOURCE
    return (const char*)x + namelen;
#define _GNU_SOURCE
  }
#define _GNU_SOURCE
  return 0;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
JSValue
#define _GNU_SOURCE
minnet_request_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
#define _GNU_SOURCE
  JSValue proto, obj;
#define _GNU_SOURCE
  MinnetRequest* req;
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(!(req = js_mallocz(ctx, sizeof(MinnetRequest))))
#define _GNU_SOURCE
    return JS_ThrowOutOfMemory(ctx);
#define _GNU_SOURCE

#define _GNU_SOURCE
    /* using new_target to get the prototype is necessary when the
  #define _GNU_SOURCE
       class is extended. */
#define _GNU_SOURCE
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
#define _GNU_SOURCE
  if(JS_IsException(proto))
#define _GNU_SOURCE
    proto = JS_DupValue(ctx, minnet_request_proto);
#define _GNU_SOURCE

#define _GNU_SOURCE
  obj = JS_NewObjectProtoClass(ctx, proto, minnet_request_class_id);
#define _GNU_SOURCE
  JS_FreeValue(ctx, proto);
#define _GNU_SOURCE
  if(JS_IsException(obj))
#define _GNU_SOURCE
    goto fail;
#define _GNU_SOURCE

#define _GNU_SOURCE
  request_zero(req);
#define _GNU_SOURCE

#define _GNU_SOURCE
  JS_SetOpaque(obj, req);
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(argc >= 1) {
#define _GNU_SOURCE
    if(JS_IsString(argv[0])) {
#define _GNU_SOURCE
      const char* str = JS_ToCString(ctx, argv[0]);
#define _GNU_SOURCE
      req->url = js_strdup(ctx, str);
#define _GNU_SOURCE
      JS_FreeCString(ctx, str);
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
    argc--;
#define _GNU_SOURCE
    argv++;
#define _GNU_SOURCE
  }
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(argc >= 1 && JS_IsObject(argv[0]))
#define _GNU_SOURCE
    js_copy_properties(ctx, obj, argv[0], JS_GPN_STRING_MASK);
#define _GNU_SOURCE

#define _GNU_SOURCE
  req->read_only = TRUE;
#define _GNU_SOURCE
  return obj;
#define _GNU_SOURCE

#define _GNU_SOURCE
fail:
#define _GNU_SOURCE
  js_free(ctx, req);
#define _GNU_SOURCE
  JS_FreeValue(ctx, obj);
#define _GNU_SOURCE
  return JS_EXCEPTION;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
JSValue
#define _GNU_SOURCE
minnet_request_new(JSContext* ctx, const char* path, const char* url, enum http_method method) {
#define _GNU_SOURCE
  struct http_request* req;
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(!(req = request_new(ctx, 0, 0, 0)))
#define _GNU_SOURCE
    return JS_ThrowOutOfMemory(ctx);
#define _GNU_SOURCE

#define _GNU_SOURCE
  request_init(req, path, js_strdup(ctx, url), method);
#define _GNU_SOURCE

#define _GNU_SOURCE
  return minnet_request_wrap(ctx, req);
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
JSValue
#define _GNU_SOURCE
minnet_request_wrap(JSContext* ctx, struct http_request* req) {
#define _GNU_SOURCE
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_request_proto, minnet_request_class_id);
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(JS_IsException(ret))
#define _GNU_SOURCE
    return JS_EXCEPTION;
#define _GNU_SOURCE

#define _GNU_SOURCE
  JS_SetOpaque(ret, req);
#define _GNU_SOURCE

#define _GNU_SOURCE
  ++req->ref_count;
#define _GNU_SOURCE

#define _GNU_SOURCE
  return ret;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
static JSValue
#define _GNU_SOURCE
minnet_request_get(JSContext* ctx, JSValueConst this_val, int magic) {
#define _GNU_SOURCE
  MinnetRequest* req;
#define _GNU_SOURCE
  JSValue ret = JS_UNDEFINED;
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(!(req = minnet_request_data2(ctx, this_val)))
#define _GNU_SOURCE
    return JS_EXCEPTION;
#define _GNU_SOURCE

#define _GNU_SOURCE
  switch(magic) {
#define _GNU_SOURCE
    case REQUEST_METHOD: {
#define _GNU_SOURCE
      ret = JS_NewString(ctx, method_name(req->method));
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
    case REQUEST_TYPE: {
#define _GNU_SOURCE
      ret = JS_NewInt32(ctx, req->method);
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
    case REQUEST_URI: {
#define _GNU_SOURCE
      if(req->url)
#define _GNU_SOURCE
        ret = JS_NewString(ctx, req->url);
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
    case REQUEST_PATH: {
#define _GNU_SOURCE
      ret = req->path[0] ? JS_NewString(ctx, req->path) : JS_NULL;
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
    case REQUEST_HEADERS: {
#define _GNU_SOURCE
      ret = header_object(ctx, &req->headers);
#define _GNU_SOURCE
      // ret = buffer_tostring(&req->headers, ctx);
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
    case REQUEST_ARRAYBUFFER: {
#define _GNU_SOURCE
      ret = buffer_WRITE(&req->body) ? buffer_toarraybuffer(&req->body, ctx) : JS_NULL;
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
    case REQUEST_TEXT: {
#define _GNU_SOURCE
      ret = buffer_WRITE(&req->body) ? buffer_tostring(&req->body, ctx) : JS_NULL;
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
    case REQUEST_BODY: {
#define _GNU_SOURCE
      if(buffer_WRITE(&req->body)) {
#define _GNU_SOURCE
        size_t typelen;
#define _GNU_SOURCE
        const char* type = header_get(ctx, &typelen, &req->headers, "content-type");
#define _GNU_SOURCE

#define _GNU_SOURCE
        ret = buffer_tostring(&req->body, ctx);
#define _GNU_SOURCE
        //  ret = minnet_stream_new(ctx, type, typelen, buffer_BEGIN(&req->body), buffer_WRITE(&req->body));
#define _GNU_SOURCE
      } else {
#define _GNU_SOURCE
        ret = JS_NULL;
#define _GNU_SOURCE
      }
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
  }
#define _GNU_SOURCE
  return ret;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
static JSValue
#define _GNU_SOURCE
minnet_request_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
#define _GNU_SOURCE
  MinnetRequest* req;
#define _GNU_SOURCE
  JSValue ret = JS_UNDEFINED;
#define _GNU_SOURCE
  const char* str;
#define _GNU_SOURCE
  size_t len;
#define _GNU_SOURCE
  if(!(req = JS_GetOpaque2(ctx, this_val, minnet_request_class_id)))
#define _GNU_SOURCE
    return JS_EXCEPTION;
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(req->read_only)
#define _GNU_SOURCE
    return JS_ThrowReferenceError(ctx, "Request object is read-only");
#define _GNU_SOURCE

#define _GNU_SOURCE
  str = JS_ToCStringLen(ctx, &len, value);
#define _GNU_SOURCE

#define _GNU_SOURCE
  switch(magic) {
#define _GNU_SOURCE
    case REQUEST_METHOD:
#define _GNU_SOURCE
    case REQUEST_TYPE: {
#define _GNU_SOURCE
      int32_t m = -1;
#define _GNU_SOURCE
      if(JS_IsNumber(value))
#define _GNU_SOURCE
        JS_ToInt32(ctx, &m, value);
#define _GNU_SOURCE
      else
#define _GNU_SOURCE
        m = name2method(str);
#define _GNU_SOURCE
      if(m >= 0 && method2name(m))
#define _GNU_SOURCE
        req->method = m;
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
    case REQUEST_URI: {
#define _GNU_SOURCE
      if(req->url) {
#define _GNU_SOURCE
        js_free(ctx, req->url);
#define _GNU_SOURCE
        req->url = 0;
#define _GNU_SOURCE
      }
#define _GNU_SOURCE
      req->url = js_strdup(ctx, str);
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
    case REQUEST_PATH: {
#define _GNU_SOURCE
      pstrcpy(req->path, sizeof(req->path), str);
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
    case REQUEST_HEADERS: {
#define _GNU_SOURCE
      ret = JS_ThrowReferenceError(ctx, "Cannot set headers");
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
  }
#define _GNU_SOURCE

#define _GNU_SOURCE
  JS_FreeCString(ctx, str);
#define _GNU_SOURCE

#define _GNU_SOURCE
  return ret;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
static void
#define _GNU_SOURCE
minnet_request_finalizer(JSRuntime* rt, JSValue val) {
#define _GNU_SOURCE
  MinnetRequest* req = JS_GetOpaque(val, minnet_request_class_id);
#define _GNU_SOURCE
  if(req && --req->ref_count == 0) {
#define _GNU_SOURCE
    if(req->url)
#define _GNU_SOURCE
      js_free_rt(rt, req->url);
#define _GNU_SOURCE

#define _GNU_SOURCE
    js_free_rt(rt, req);
#define _GNU_SOURCE
  }
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
JSClassDef minnet_request_class = {
#define _GNU_SOURCE
    "MinnetRequest",
#define _GNU_SOURCE
    .finalizer = minnet_request_finalizer,
#define _GNU_SOURCE
};
#define _GNU_SOURCE

#define _GNU_SOURCE
const JSCFunctionListEntry minnet_request_proto_funcs[] = {
#define _GNU_SOURCE
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_request_get, minnet_request_set, REQUEST_TYPE, 0),
#define _GNU_SOURCE
    JS_CGETSET_MAGIC_FLAGS_DEF("method", minnet_request_get, minnet_request_set, REQUEST_METHOD, JS_PROP_ENUMERABLE),
#define _GNU_SOURCE
    JS_CGETSET_MAGIC_FLAGS_DEF("url", minnet_request_get, minnet_request_set, REQUEST_URI, JS_PROP_ENUMERABLE),
#define _GNU_SOURCE
    JS_CGETSET_MAGIC_FLAGS_DEF("path", minnet_request_get, 0, REQUEST_PATH, JS_PROP_ENUMERABLE),
#define _GNU_SOURCE
    JS_CGETSET_MAGIC_FLAGS_DEF("headers", minnet_request_get, 0, REQUEST_HEADERS, 0),
#define _GNU_SOURCE
    JS_CGETSET_MAGIC_FLAGS_DEF("arrayBuffer", minnet_request_get, 0, REQUEST_ARRAYBUFFER, 0),
#define _GNU_SOURCE
    JS_CGETSET_MAGIC_FLAGS_DEF("text", minnet_request_get, 0, REQUEST_TEXT, 0),
#define _GNU_SOURCE
    JS_CGETSET_MAGIC_FLAGS_DEF("body", minnet_request_get, 0, REQUEST_BODY, 0),
#define _GNU_SOURCE
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetRequest", JS_PROP_CONFIGURABLE),
#define _GNU_SOURCE
};
#define _GNU_SOURCE

#define _GNU_SOURCE
const size_t minnet_request_proto_funcs_size = countof(minnet_request_proto_funcs);

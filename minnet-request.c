#define _GNU_SOURCE
#include <quickjs.h>
#include <cutils.h>
#include "minnet-request.h"
#include "minnet-stream.h"
#include "jsutils.h"
#include <ctype.h>
#include <strings.h>

THREAD_LOCAL JSClassID minnet_request_class_id;
THREAD_LOCAL JSValue minnet_request_proto, minnet_request_ctor;

enum { REQUEST_TYPE, REQUEST_METHOD, REQUEST_URI, REQUEST_PATH, REQUEST_HEADERS, REQUEST_ARRAYBUFFER, REQUEST_TEXT, REQUEST_BODY };

static const char* const method_names[] = {"GET", "POST", "OPTIONS", "PUT", "PATCH", "DELETE", "CONNECT", "HEAD"};

const char*
method_string(enum http_method m) {
  if(m >= 0 && m < countof(method_names))
    return method_names[m];
  return 0;
}

int
method_number(const char* name) {
  int i = 0;
  if(name)
    for(i = countof(method_names) - 1; i >= 0; --i)
      if(!strcasecmp(name, method_names[i]))
        break;
  return i;
}

void
request_format(struct http_request const* req, char* buf, size_t len, JSContext* ctx) {
  char* headers = buffer_escaped(&req->headers, ctx);

  snprintf(buf, len, FGC(196, "MinnetRequest") " { method: '%s', url: '%s', path: '%s', headers: '%s' }", method_name(req->method), req->url, req->path, headers);

  js_free(ctx, headers);
}

char*
request_dump(struct http_request const* req, JSContext* ctx) {
  static char buf[2048];
  request_format(req, buf, sizeof(buf), ctx);
  return buf;
}

void
request_init(struct http_request* req, const char* path, char* url, enum http_method method) {
  memset(req, 0, sizeof(*req));

  req->ref_count = 0;

  if(path)
    pstrcpy(req->path, sizeof(req->path), path);

  req->url = url;
  req->method = method;
}

struct http_request*
request_new(JSContext* ctx, const char* path, char* url, MinnetHttpMethod method) {
  MinnetRequest* req;

  if((req = js_mallocz(ctx, sizeof(MinnetRequest))))
    request_init(req, path, url, method);

  return req;
}

struct http_request*
request_from(JSContext* ctx, JSValueConst options) {
  MinnetRequest* req;
  JSValue value;
  const char *url, *path, *method;

  value = JS_GetPropertyStr(ctx, options, "url");
  url = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, options, "path");
  path = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  JS_GetPropertyStr(ctx, options, "method");
  method = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  JS_GetPropertyStr(ctx, options, "headers");

  JS_FreeValue(ctx, value);

  request_init(req, path, url, method_number(method));

  JS_FreeCString(ctx, url);
  JS_FreeCString(ctx, path);
  JS_FreeCString(ctx, method);

  return req;
}

void
request_zero(struct http_request* req) {
  memset(req, 0, sizeof(MinnetRequest));
  req->headers = BUFFER_0();
  req->body = BUFFER_0();
}

static const char*
header_get(JSContext* ctx, size_t* lenp, MinnetBuffer* buf, const char* name) {
  size_t len, namelen = strlen(name);
  uint8_t *x, *end;

  for(x = buf->start, end = buf->write; x < end; x += len + 1) {
    len = byte_chr(x, end - x, '\n');

    /*   if(namelen >= len)
         continue;*/
    if(byte_chr(x, len, ':') != namelen || strncasecmp(name, (const char*)x, namelen))
      continue;

    if(x[namelen] == ':')
      namelen++;
    if(isspace(x[namelen]))
      namelen++;

    if(lenp)
      *lenp = len - namelen;
    return (const char*)x + namelen;
  }
  return 0;
}

JSValue
minnet_request_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetRequest* req;

  if(!(req = js_mallocz(ctx, sizeof(MinnetRequest))))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_request_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_request_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  request_zero(req);

  JS_SetOpaque(obj, req);

  if(argc >= 1) {
    if(JS_IsString(argv[0])) {
      const char* str = JS_ToCString(ctx, argv[0]);
      req->url = js_strdup(ctx, str);
      JS_FreeCString(ctx, str);
    }
    argc--;
    argv++;
  }

  if(argc >= 1 && JS_IsObject(argv[0]))
    js_copy_properties(ctx, obj, argv[0], JS_GPN_STRING_MASK);

  req->read_only = TRUE;
  return obj;

fail:
  js_free(ctx, req);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
minnet_request_new(JSContext* ctx, const char* path, const char* url, enum http_method method) {
  struct http_request* req;

  if(!(req = request_new(ctx, 0, 0, 0)))
    return JS_ThrowOutOfMemory(ctx);

  request_init(req, path, js_strdup(ctx, url), method);

  return minnet_request_wrap(ctx, req);
}

JSValue
minnet_request_wrap(JSContext* ctx, struct http_request* req) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_request_proto, minnet_request_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, req);

  ++req->ref_count;

  return ret;
}

static JSValue
minnet_request_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetRequest* req;
  JSValue ret = JS_UNDEFINED;

  if(!(req = minnet_request_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case REQUEST_METHOD: {
      ret = JS_NewString(ctx, method_name(req->method));
      break;
    }
    case REQUEST_TYPE: {
      ret = JS_NewInt32(ctx, req->method);
      break;
    }
    case REQUEST_URI: {
      if(req->url)
        ret = JS_NewString(ctx, req->url);
      break;
    }
    case REQUEST_PATH: {
      ret = req->path[0] ? JS_NewString(ctx, req->path) : JS_NULL;
      break;
    }
    case REQUEST_HEADERS: {
      ret = headers_object(ctx, &req->headers);
      // ret = buffer_tostring(&req->headers, ctx);
      break;
    }
    case REQUEST_ARRAYBUFFER: {
      ret = buffer_HEAD(&req->body) ? buffer_toarraybuffer(&req->body, ctx) : JS_NULL;
      break;
    }
    case REQUEST_TEXT: {
      ret = buffer_HEAD(&req->body) ? buffer_tostring(&req->body, ctx) : JS_NULL;
      break;
    }
    case REQUEST_BODY: {
      if(buffer_HEAD(&req->body)) {
        size_t typelen;
        const char* type = header_get(ctx, &typelen, &req->headers, "content-type");

        ret = buffer_tostring(&req->body, ctx);
        //  ret = minnet_stream_new(ctx, type, typelen, block_BEGIN(&req->body), buffer_HEAD(&req->body));
      } else {
        ret = JS_NULL;
      }
      break;
    }
  }
  return ret;
}

static JSValue
minnet_request_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetRequest* req;
  JSValue ret = JS_UNDEFINED;
  const char* str;
  size_t len;
  if(!(req = JS_GetOpaque2(ctx, this_val, minnet_request_class_id)))
    return JS_EXCEPTION;

  if(req->read_only)
    return JS_ThrowReferenceError(ctx, "Request object is read-only");

  str = JS_ToCStringLen(ctx, &len, value);

  switch(magic) {
    case REQUEST_METHOD:
    case REQUEST_TYPE: {
      int32_t m = -1;
      if(JS_IsNumber(value))
        JS_ToInt32(ctx, &m, value);
      else
        m = method_number(str);
      if(m >= 0 && method_string(m))
        req->method = m;
      break;
    }
    case REQUEST_URI: {
      if(req->url) {
        js_free(ctx, req->url);
        req->url = 0;
      }
      req->url = js_strdup(ctx, str);
      break;
    }
    case REQUEST_PATH: {
      pstrcpy(req->path, sizeof(req->path), str);
      break;
    }
    case REQUEST_HEADERS: {
      ret = JS_ThrowReferenceError(ctx, "Cannot set headers");
      break;
    }
  }

  JS_FreeCString(ctx, str);

  return ret;
}

static void
minnet_request_finalizer(JSRuntime* rt, JSValue val) {
  MinnetRequest* req = JS_GetOpaque(val, minnet_request_class_id);
  if(req && --req->ref_count == 0) {
    if(req->url)
      js_free_rt(rt, req->url);

    js_free_rt(rt, req);
  }
}

JSClassDef minnet_request_class = {
    "MinnetRequest",
    .finalizer = minnet_request_finalizer,
};

const JSCFunctionListEntry minnet_request_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_request_get, minnet_request_set, REQUEST_TYPE, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("method", minnet_request_get, minnet_request_set, REQUEST_METHOD, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("url", minnet_request_get, minnet_request_set, REQUEST_URI, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("path", minnet_request_get, 0, REQUEST_PATH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("headers", minnet_request_get, 0, REQUEST_HEADERS, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("arrayBuffer", minnet_request_get, 0, REQUEST_ARRAYBUFFER, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("text", minnet_request_get, 0, REQUEST_TEXT, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("body", minnet_request_get, 0, REQUEST_BODY, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetRequest", JS_PROP_CONFIGURABLE),
};

const size_t minnet_request_proto_funcs_size = countof(minnet_request_proto_funcs);

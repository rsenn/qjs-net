#define _GNU_SOURCE
#include <quickjs.h>
#include <cutils.h>
#include "minnet-request.h"
#include "minnet-url.h"
#include "minnet-stream.h"
#include "jsutils.h"
#include <ctype.h>
#include <strings.h>

THREAD_LOCAL JSClassID minnet_request_class_id;
THREAD_LOCAL JSValue minnet_request_proto, minnet_request_ctor;

enum { REQUEST_TYPE, REQUEST_METHOD, REQUEST_URI, REQUEST_PATH, REQUEST_HEADERS, REQUEST_ARRAYBUFFER, REQUEST_TEXT, REQUEST_BODY };

static const char* const method_names[] = {
    "GET",
    "POST",
    "OPTIONS",
    "PUT",
    "PATCH",
    "DELETE",
    "CONNECT",
    "HEAD",
};

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
request_format(MinnetRequest const* req, char* buf, size_t len, JSContext* ctx) {
  char* headers = buffer_escaped(&req->headers, ctx);
  char* url = url_format(&req->url, ctx);
  snprintf(buf, len, FGC(196, "MinnetRequest") " { method: '%s', url: '%s', headers: '%s' }", method_name(req->method), url, headers);

  js_free(ctx, headers);
  js_free(ctx, url);
}

char*
request_dump(MinnetRequest const* req, JSContext* ctx) {
  static char buf[2048];
  request_format(req, buf, sizeof(buf), ctx);
  return buf;
}

void
request_init(MinnetRequest* req, MinnetURL url, enum http_method method) {
  memset(req, 0, sizeof(MinnetRequest));

  req->url = url;
  req->method = method;
}

MinnetRequest*
request_alloc(JSContext* ctx) {
  MinnetRequest* ret;

  ret = js_mallocz(ctx, sizeof(MinnetRequest));
  ret->ref_count = 1;
  return ret;
}

MinnetRequest*
request_new(JSContext* ctx, MinnetURL url, MinnetHttpMethod method) {
  MinnetRequest* req;

  if((req = request_alloc(ctx)))
    request_init(req, url, method);

  return req;
}

MinnetRequest*
request_dup(MinnetRequest* req) {
  ++req->ref_count;
  return req;
}

MinnetRequest*
request_fromobj(JSContext* ctx, JSValueConst options) {
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

  request_init(req, /*path,*/ url_create(url, ctx), method_number(method));

  JS_FreeCString(ctx, url);
  JS_FreeCString(ctx, path);
  JS_FreeCString(ctx, method);

  return req;
}

void
request_zero(MinnetRequest* req) {
  memset(req, 0, sizeof(MinnetRequest));
  req->headers = BUFFER_0();
  req->body = BUFFER_0();
}

void
request_clear(MinnetRequest* req, JSContext* ctx) {
  url_free(&req->url, ctx);
  buffer_free(&req->headers, JS_GetRuntime(ctx));
  buffer_free(&req->body, JS_GetRuntime(ctx));
}

void
request_clear_rt(MinnetRequest* req, JSRuntime* rt) {
  url_free_rt(&req->url, rt);
  buffer_free(&req->headers, rt);
  buffer_free(&req->body, rt);
}

void
request_free(MinnetRequest* req, JSContext* ctx) {
  if(--req->ref_count == 0) {
    request_clear(req, ctx);
    js_free(ctx, req);
  }
}
void
request_free_rt(MinnetRequest* req, JSRuntime* rt) {
  if(--req->ref_count == 0) {
    request_clear_rt(req, rt);
    js_free_rt(rt, req);
  }
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

MinnetRequest*
request_from(JSContext* ctx, int argc, JSValueConst argv[]) {
  MinnetRequest* req = 0;
  MinnetURL url = {0, 0, 0, 0};

  if(JS_IsObject(argv[0]) && (req = minnet_request_data(argv[0]))) {
    req = request_dup(req);
  } else {
    url_from(&url, argv[0], ctx);

    if(url_valid(&url))
      req = request_new(ctx, url, METHOD_GET);
  }

  if(req)
    if(argc >= 2 && JS_IsObject(argv[1])) {
      JSValue headers = JS_GetPropertyStr(ctx, argv[1], "headers");
      if(!JS_IsUndefined(headers))
        headers_fromobj(&req->headers, headers, ctx);

      JS_FreeValue(ctx, headers);
    }

  return req;
}

JSValue
minnet_request_from(JSContext* ctx, int argc, JSValueConst argv[]) {
  MinnetRequest* req;

  /* if(JS_IsObject(argv[0]) && (req = minnet_request_data(argv[0])))
     req = request_dup(req);
   else*/
  req = request_from(ctx, argc, argv);

  return minnet_request_wrap(ctx, req);
}

JSValue
minnet_request_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetRequest* req;
  BOOL got_url = FALSE;

  if(!(req = request_alloc(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_request_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_request_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, req);

  while(argc > 0) {

    if(!got_url) {
      got_url = url_from(&req->url, argv[0], ctx);

    } else if(JS_IsObject(argv[0])) {
      js_copy_properties(ctx, obj, argv[0], JS_GPN_STRING_MASK);
    }

    argc--;
    argv++;
  }

  req->read_only = TRUE;
  return obj;

fail:
  js_free(ctx, req);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
minnet_request_new(JSContext* ctx, MinnetURL url, enum http_method method) {
  MinnetRequest* req;

  if(!(req = request_new(ctx, url, method)))
    return JS_ThrowOutOfMemory(ctx);

  return minnet_request_wrap(ctx, req);
}

JSValue
minnet_request_wrap(JSContext* ctx, MinnetRequest* req) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_request_proto, minnet_request_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, req);

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
      ret = minnet_url_new(ctx, req->url);
      break;
    }
    case REQUEST_PATH: {
      ret = req->url.path ? JS_NewString(ctx, req->url.path) : JS_NULL;
      break;
    }
    case REQUEST_HEADERS: {
      ret = headers_object(ctx, req->headers.start, req->headers.end);
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
  if(!(req = minnet_request_data2(ctx, this_val)))
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
      url_free(&req->url, ctx);
      url_parse(&req->url, str, ctx);
      break;
    }
    case REQUEST_PATH: {
      if(req->url.path) {
        js_free(ctx, req->url.path);
        req->url.path = 0;
      }
      req->url.path = js_strdup(ctx, str);
      break;
    }
    case REQUEST_HEADERS: {

      if(JS_IsObject(value)) {
        headers_fromobj(&req->headers, value, ctx);
      } else {
        const char* str = JS_ToCString(ctx, value);
        ret = JS_ThrowReferenceError(ctx, "Cannot set headers to '%s'", str);
        JS_FreeCString(ctx, str);
      }
      break;
    }
  }

  JS_FreeCString(ctx, str);

  return ret;
}

static void
minnet_request_finalizer(JSRuntime* rt, JSValue val) {
  MinnetRequest* req;

  if((req = minnet_request_data(val)))
    request_free_rt(req, rt);
}

JSClassDef minnet_request_class = {
    "MinnetRequest",
    .finalizer = minnet_request_finalizer,
};

const JSCFunctionListEntry minnet_request_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_request_get, minnet_request_set, REQUEST_TYPE, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("method", minnet_request_get, minnet_request_set, REQUEST_METHOD, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("url", minnet_request_get, minnet_request_set, REQUEST_URI, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("path", minnet_request_get, minnet_request_set, REQUEST_PATH, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("headers", minnet_request_get, minnet_request_set, REQUEST_HEADERS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("arrayBuffer", minnet_request_get, 0, REQUEST_ARRAYBUFFER, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("text", minnet_request_get, 0, REQUEST_TEXT, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("body", minnet_request_get, 0, REQUEST_BODY, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetRequest", JS_PROP_CONFIGURABLE),
};

const size_t minnet_request_proto_funcs_size = countof(minnet_request_proto_funcs);

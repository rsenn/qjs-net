#define _GNU_SOURCE
#include <quickjs.h>
#include <cutils.h>
#include "minnet-request.h"
#include "minnet-ringbuffer.h"
#include "minnet-generator.h"
#include "minnet.h"
#include "headers.h"
#include "jsutils.h"
#include <ctype.h>
#include <strings.h>
#include <libwebsockets.h>

THREAD_LOCAL JSClassID minnet_request_class_id;
THREAD_LOCAL JSValue minnet_request_proto, minnet_request_ctor;

enum {
  REQUEST_TYPE,
  REQUEST_METHOD,
  REQUEST_URI,
  REQUEST_PATH,
  REQUEST_HEADERS,
  REQUEST_ARRAYBUFFER,
  REQUEST_TEXT,
  REQUEST_BODY,
  REQUEST_IP,
  REQUEST_H2,
  REQUEST_PROTOCOL,
  REQUEST_SECURE,
  REQUEST_REFERER,
};

MinnetRequest*
minnet_request_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_request_class_id);
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
      got_url = url_fromvalue(&req->url, argv[0], ctx);
    }
    if(JS_IsObject(argv[0])) {
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
minnet_request_clone(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetRequest *req, *req2;

  if(!(req = minnet_request_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if((req2 = request_new(url_clone(req->url, ctx), req->method, ctx)))
    return minnet_request_wrap(ctx, req2);

  return JS_ThrowOutOfMemory(ctx);
}

JSValue
minnet_request_wrap(JSContext* ctx, MinnetRequest* req) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_request_proto, minnet_request_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, request_dup(req));

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
      char* type;

      if((type = headers_get(&req->headers, "content-type", ctx))) {
        ret = JS_NewString(ctx, type);
        js_free(ctx, type);
      }
      break;
    }
    case REQUEST_URI: {
      ret = minnet_url_new(ctx, req->url);
      break;
    }
    case REQUEST_PATH: {
      ret = JS_NewString(ctx, req->url.path ? req->url.path : "");
      break;
    }
    case REQUEST_HEADERS: {
      DEBUG("REQUEST_HEADERS start=%p, end=%p\n", req->headers.start, req->headers.end);

      if(buffer_BYTES(&req->headers))
        ret = headers_object(ctx, req->headers.start, req->headers.write);
      // ret = buffer_tostring(&req->headers, ctx);
      break;
    }
    case REQUEST_REFERER: {
      char* ref;

      if((ref = headers_get(&req->headers, "referer", ctx))) {
        ret = JS_NewString(ctx, ref);
        js_free(ctx, ref);
      }

      break;
    }

    case REQUEST_BODY: {
      switch(req->method) {
        case METHOD_GET:
        case METHOD_OPTIONS:
        case METHOD_PATCH:
        case METHOD_PUT:
        case METHOD_DELETE:
        case METHOD_HEAD:
          if(!req->body)
            break;

        case METHOD_POST: ret = minnet_generator_create(ctx, &req->body); break;
      }
      break;
    }
    case REQUEST_IP: {
      ret = req->ip ? JS_NewString(ctx, req->ip) : JS_NULL;
      break;
    }
    case REQUEST_PROTOCOL: {
      ret = JS_NewString(ctx, req->url.protocol);
      break;
    }
    case REQUEST_SECURE: {
      ret = JS_NewBool(ctx, req->secure);
      break;
    }
    case REQUEST_H2: {
      ret = JS_NewBool(ctx, req->h2);
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

static JSValue
minnet_request_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MinnetRequest* req;
  JSValue ret = JS_UNDEFINED;

  if(!(req = minnet_request_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {

    case REQUEST_ARRAYBUFFER: {
      if(req->body) {
        ResolveFunctions funcs = {JS_NULL, JS_NULL};
        ret = js_promise_create(ctx, &funcs);
        JS_FreeValue(ctx, funcs.reject);

        generator_continuous(req->body, funcs.resolve);
        req->body->block_fn = &block_toarraybuffer;
      }
      break;
    }
    case REQUEST_TEXT: {
      if(req->body) {
        ResolveFunctions funcs = {JS_NULL, JS_NULL};
        ret = js_promise_create(ctx, &funcs);
        JS_FreeValue(ctx, funcs.reject);

        generator_continuous(req->body, funcs.resolve);
        req->body->block_fn = &block_tostring;
      }
      break;
    }
  }
  return ret;
}

static JSValue
minnet_request_getheader(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetRequest* req;
  JSValue ret = JS_UNDEFINED;
  const char *key, *value;

  if(!(req = minnet_request_data2(ctx, this_val)))
    return JS_EXCEPTION;

  key = JS_ToCString(ctx, argv[0]);

  value = headers_get(&req->headers, key, ctx);

  ret = JS_NewString(ctx, value);

  js_free(ctx, (void*)value);
  JS_FreeCString(ctx, key);

  return ret;
}

static void
minnet_request_finalizer(JSRuntime* rt, JSValue val) {
  MinnetRequest* req;

  if((req = minnet_request_data(val)))
    request_free(req, rt);
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
    JS_CGETSET_MAGIC_FLAGS_DEF("referer", minnet_request_get, 0, REQUEST_REFERER, 0),
    JS_CFUNC_MAGIC_DEF("arrayBuffer", 0, minnet_request_method, REQUEST_ARRAYBUFFER),
    JS_CFUNC_MAGIC_DEF("text", 0, minnet_request_method, REQUEST_TEXT),
    JS_CGETSET_MAGIC_FLAGS_DEF("body", minnet_request_get, 0, REQUEST_BODY, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("secure", minnet_request_get, 0, REQUEST_SECURE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("ip", minnet_request_get, 0, REQUEST_IP, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("h2", minnet_request_get, 0, REQUEST_H2, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("protocol", minnet_request_get, 0, REQUEST_PROTOCOL, 0),
    JS_CFUNC_DEF("get", 1, minnet_request_getheader),
    JS_CFUNC_DEF("clone", 0, minnet_request_clone),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetRequest", JS_PROP_CONFIGURABLE),
};

const size_t minnet_request_proto_funcs_size = countof(minnet_request_proto_funcs);

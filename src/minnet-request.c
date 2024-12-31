#define _GNU_SOURCE
#include <quickjs.h>
#include <cutils.h>
#include "minnet-request.h"
#include "minnet-ringbuffer.h"
#include "minnet-generator.h"
#include "minnet-headers.h"
#include "minnet.h"
#include "headers.h"
#include "js-utils.h"
#include <ctype.h>
#include <strings.h>
#include <libwebsockets.h>
#include <assert.h>

THREAD_LOCAL JSClassID minnet_request_class_id;
THREAD_LOCAL JSValue minnet_request_proto, minnet_request_ctor;

MinnetRequest*
minnet_request_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_request_class_id);
}

JSValue
minnet_request_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetRequest *req, *other;
  BOOL got_url = FALSE;

  if(!(req = request_alloc(ctx)))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_request_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_request_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, req);

  if((other = minnet_request_data(argv[0]))) {
    req->secure = other->secure;
    req->h2 = other->h2;
    req->method = other->method;
    req->url = url_clone(other->url, ctx);
    buffer_clone(&req->headers, &other->headers);

  } else {
    url_fromvalue(&req->url, argv[0], ctx);
  }

  if(argc > 1 && JS_IsObject(argv[1]))
    js_copy_properties(ctx, obj, argv[1], JS_GPN_STRING_MASK);

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

  return JS_EXCEPTION;
}

JSValue
minnet_request_wrap(JSContext* ctx, MinnetRequest* req) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_request_proto, minnet_request_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, request_dup(req));

  return ret;
}

enum {
  REQUEST_BODY,
  REQUEST_H2,
  REQUEST_HEADERS,
  REQUEST_IP,
  REQUEST_METHOD,
  REQUEST_PATH,
  REQUEST_PROTOCOL,
  REQUEST_REFERER,
  REQUEST_SECURE,
  REQUEST_TYPE,
  REQUEST_URI,
};

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

      if((type = headers_get(&req->headers, "content-type", "\r\n", ":", ctx))) {
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
      ret = minnet_headers_wrap(ctx, &req->headers, request_dup(req), (HeadersFreeFunc*)&request_free);
      // minnet_headers_value(ctx,  this_val);
      break;
    }

    case REQUEST_REFERER: {
      char* ref;

      if((ref = headers_get(&req->headers, "referer", "\r\n", ":", ctx))) {
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
        case METHOD_HEAD: {
          assert(!req->body);
          ret = JS_NULL;
          break;
        }

        case METHOD_POST: {
          ret = minnet_generator_create(ctx, &req->body);
          break;
        }
      }
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
      url_free(&req->url, JS_GetRuntime(ctx));
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

      if(JS_IsObject(value))
        headers_fromobj(&req->headers, value, "\n", ": ", ctx);
      else
        ret = JS_ThrowReferenceError(ctx, "headers must be an object");

      break;
    }
  }

  JS_FreeCString(ctx, str);

  return ret;
}

enum {
  REQUEST_ARRAYBUFFER,
  REQUEST_TEXT,
  REQUEST_JSON,
};

static JSValue
minnet_request_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  ResolveFunctions funcs;
  MinnetRequest* req;
  Generator* gen;

  if(!(req = minnet_request_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if((gen = req->body)) {
    ret = js_async_create(ctx, &funcs);

    switch(magic) {
      case REQUEST_ARRAYBUFFER: {
        generator_continuous(gen, funcs.resolve);
        break;
      }
      case REQUEST_TEXT: {
        JSValue tmp, fn = JS_NewCFunction(ctx, js_arraybuffer_tostring, "", 1);

        tmp = js_invoke(ctx, ret, "then", 1, &fn);
        JS_FreeValue(ctx, fn);
        JS_FreeValue(ctx, ret);
        ret = tmp;

        generator_continuous(gen, funcs.resolve);
        break;
      }
      case REQUEST_JSON: {
        JSValue tmp, fn = JS_NewCFunction(ctx, js_arraybuffer_tostring, "", 1);

        tmp = js_invoke(ctx, ret, "then", 1, &fn);
        JS_FreeValue(ctx, fn);
        JS_FreeValue(ctx, ret);
        ret = tmp;

        fn = js_global_static_func(ctx, "JSON", "parse");

        tmp = js_invoke(ctx, ret, "then", 1, &fn);
        JS_FreeValue(ctx, fn);
        JS_FreeValue(ctx, ret);
        ret = tmp;

        generator_continuous(gen, funcs.resolve);
        break;
      }
    }

    js_async_free(JS_GetRuntime(ctx), &funcs);
  }

  return ret;
}
/*static JSValue
minnet_request_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MinnetRequest* req;
  JSValue ret = JS_UNDEFINED;

  if(!(req = minnet_request_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!js_is_promise(ctx, req->promise))
    return JS_ThrowInternalError(ctx, "no Request Promise");

  ret = JS_DupValue(ctx, req->promise);

  switch(magic) {
    case REQUEST_ARRAYBUFFER: {
      req->body->block_fn = &block_toarraybuffer;
      break;
    }

    case REQUEST_TEXT: {
      req->body->block_fn = &block_tostring;
      break;
    }

    case REQUEST_JSON: {
      req->body->block_fn = &block_tojson;
      break;
    }
  }
  return ret;
}
*/
static JSValue
minnet_request_getheader(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetRequest* req;
  JSValue ret = JS_UNDEFINED;
  const char *key, *value;

  if(!(req = minnet_request_data2(ctx, this_val)))
    return JS_EXCEPTION;

  key = JS_ToCString(ctx, argv[0]);

  if((value = headers_get(&req->headers, key, "\r\n", ":", ctx))) {
    ret = JS_NewString(ctx, value);
    js_free(ctx, (void*)value);
  }

  JS_FreeCString(ctx, key);

  return ret;
}

static void
minnet_request_finalizer(JSRuntime* rt, JSValue val) {
  MinnetRequest* req;

  if((req = minnet_request_data(val)))
    request_free(req, rt);
}

static const JSClassDef minnet_request_class = {
    "MinnetRequest",
    .finalizer = minnet_request_finalizer,
};

static const JSCFunctionListEntry minnet_request_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_request_get, minnet_request_set, REQUEST_TYPE, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("url", minnet_request_get, minnet_request_set, REQUEST_URI, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("method", minnet_request_get, minnet_request_set, REQUEST_METHOD, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("path", minnet_request_get, minnet_request_set, REQUEST_PATH, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("protocol", minnet_request_get, 0, REQUEST_PROTOCOL, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("headers", minnet_request_get, minnet_request_set, REQUEST_HEADERS, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("referer", minnet_request_get, 0, REQUEST_REFERER, 0),
    JS_CFUNC_MAGIC_DEF("arrayBuffer", 0, minnet_request_method, REQUEST_ARRAYBUFFER),
    JS_CFUNC_MAGIC_DEF("text", 0, minnet_request_method, REQUEST_TEXT),
    JS_CFUNC_MAGIC_DEF("json", 0, minnet_request_method, REQUEST_JSON),
    JS_CGETSET_MAGIC_FLAGS_DEF("body", minnet_request_get, 0, REQUEST_BODY, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("secure", minnet_request_get, 0, REQUEST_SECURE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("h2", minnet_request_get, 0, REQUEST_H2, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    JS_CFUNC_DEF("get", 1, minnet_request_getheader),
    JS_CFUNC_DEF("clone", 0, minnet_request_clone),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetRequest", JS_PROP_CONFIGURABLE),
};

int
minnet_request_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&minnet_request_class_id);

  JS_NewClass(JS_GetRuntime(ctx), minnet_request_class_id, &minnet_request_class);

  minnet_request_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_request_proto, minnet_request_proto_funcs, countof(minnet_request_proto_funcs));
  JS_SetClassProto(ctx, minnet_request_class_id, minnet_request_proto);

  minnet_request_ctor = JS_NewCFunction2(ctx, minnet_request_constructor, "MinnetRequest", 1, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, minnet_request_ctor, minnet_request_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Request", minnet_request_ctor);

  return 0;
}

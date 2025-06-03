#include "minnet-websocket.h"
#include "minnet-response.h"
#include "minnet-generator.h"
#include "minnet-headers.h"
#include "minnet.h"
#include "buffer.h"
#include "js-utils.h"
#include "headers.h"
#include <cutils.h>
#include <assert.h>

THREAD_LOCAL JSClassID minnet_response_class_id;
THREAD_LOCAL JSValue minnet_response_proto, minnet_response_ctor;

enum { RESPONSE_HEADER };
enum {
  RESPONSE_OK,
  RESPONSE_HEADERS_SENT,
  RESPONSE_URL,
  RESPONSE_STATUS,
  RESPONSE_STATUSTEXT,
  RESPONSE_REDIRECTED,
  RESPONSE_BODYUSED,
  RESPONSE_BODY,
  RESPONSE_TYPE,
  RESPONSE_OFFSET,
  RESPONSE_HEADERS,
  RESPONSE_WRITE,
  RESPONSE_FINISH,
};

MinnetResponse*
minnet_response_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_response_class_id);
}

JSValue
minnet_response_new(JSContext* ctx, MinnetURL url, int status, const char* status_text, BOOL headers_sent, const char* type) {
  MinnetResponse* resp;

  if((resp = response_new(ctx))) {
    response_init(resp, url, status, status_text ? js_strdup(ctx, status_text) : 0, headers_sent, type ? js_strdup(ctx, type) : 0);

    return minnet_response_wrap(ctx, resp);
  }

  return JS_NULL;
}

JSValue
minnet_response_wrap(JSContext* ctx, MinnetResponse* resp) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_response_proto, minnet_response_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, response_dup(resp));
  return ret;
}

enum {
  RESPONSE_ARRAYBUFFER = 0,
  RESPONSE_TEXT,
  RESPONSE_JSON,
};

static JSValue
minnet_response_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  ResolveFunctions funcs;
  MinnetResponse* resp;
  Generator* gen;
  JSValue (*block_fn)(ByteBlock*, JSContext*) = magic == RESPONSE_ARRAYBUFFER ? &block_toarraybuffer : &block_tostring;

  if(!(resp = minnet_response_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!resp->sync)
    ret = js_async_create(ctx, &funcs);

  gen = response_generator(resp, ctx);
  gen->block_fn = block_fn;

  switch(magic) {
    case RESPONSE_ARRAYBUFFER: {

      if(resp->sync) {
        ByteBlock blk = queue_next(gen->q, NULL, NULL);
        ret = block_fn(&blk, ctx);
      } else {
        generator_continuous(gen, funcs.resolve);

        if(generator_stopped(gen))
          generator_finish(gen);
      }

      break;
    }

    case RESPONSE_TEXT: {

      if(resp->sync) {
        ByteBlock blk = queue_next(gen->q, NULL, NULL);
        ret = block_fn(&blk, ctx);
      } else {
        generator_continuous(gen, funcs.resolve);

        if(generator_stopped(gen))
          generator_finish(gen);
      }

      break;
    }

    case RESPONSE_JSON: {
      if(resp->sync) {
        ByteBlock blk = queue_next(gen->q, NULL, NULL);
        ret = block_tojson(&blk, ctx);
      } else {
        generator_continuous(gen, funcs.resolve);

        if(generator_stopped(gen))
          generator_finish(gen);
      }

      break;
    }

    case RESPONSE_WRITE: {
      ret = generator_push(gen, argv[0]);
      break;
    }

    case RESPONSE_FINISH: {
      BOOL result = generator_finish(gen);

      ret = JS_NewBool(ctx, result);
      break;
    }
  }

  if(!resp->sync)
    js_async_free(JS_GetRuntime(ctx), &funcs);

  return ret;
}

enum {
  RESPONSE_REDIRECT = 0,
  RESPONSE_ERROR,
};

static JSValue
minnet_response_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case RESPONSE_REDIRECT: {
      uint32_t status = 302;
      MinnetURL url = URL_INIT();
      url_fromvalue(&url, argv[0], ctx);
      if(argc > 1)
        JS_ToUint32(ctx, &status, argv[1]);
      ret = minnet_response_new(ctx, url, status, 0, FALSE, 0);
      break;
    }

    case RESPONSE_ERROR: {
      MinnetURL url = URL_INIT();
      ret = minnet_response_new(ctx, url, 0, 0, FALSE, 0);
      break;
    }
  }

  return ret;
}

enum {
  RESPONSE_HEADERS_GET = 0,
  RESPONSE_HEADERS_SET,
  RESPONSE_HEADERS_APPEND,
  RESPONSE_HEADERS_LOCATION,
};

static JSValue
minnet_response_header(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  MinnetResponse* resp;
  const char* key;
  size_t keylen;

  if(!(resp = minnet_response_data2(ctx, this_val)))
    return JS_EXCEPTION;

  key = JS_ToCStringLen(ctx, &keylen, argv[0]);

  switch(magic) {
    case RESPONSE_HEADERS_GET: {
      size_t vlen;
      char* v;

      if((v = headers_getlen(&resp->headers, &vlen, key, "\r\n", ":")))
        ret = JS_NewStringLen(ctx, v, vlen);

      break;
    }

    case RESPONSE_HEADERS_SET: {
      const char* v;

      if((v = JS_ToCString(ctx, argv[1])))
        ret = JS_NewInt32(ctx, headers_set(&resp->headers, key, v, "\r\n"));

      break;
    }

    case RESPONSE_HEADERS_APPEND: {
      const char* v;
      size_t vlen;

      if((v = JS_ToCStringLen(ctx, &vlen, argv[1])))
        ret = JS_NewInt32(ctx, headers_appendb(&resp->headers, key, keylen, v, vlen, "\r\n"));

      break;
    }

    case RESPONSE_HEADERS_LOCATION: {
      ret = JS_NewInt32(ctx, headers_set(&resp->headers, "Location", key, "\r\n"));
      break;
    }
  }

  return ret;
}

static JSValue
minnet_response_clone(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetResponse *resp, *clone;

  if(!(resp = minnet_response_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(clone = response_new(ctx)))
    return JS_EXCEPTION;

  clone->read_only = resp->read_only;
  clone->status = resp->status;
  clone->read_only = resp->read_only;
  clone->url = url_clone(resp->url, ctx);

  buffer_clone(&clone->headers, &resp->headers);
  // buffer_clone(clone->body, resp->body);

  return minnet_response_wrap(ctx, clone);
}

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

    case RESPONSE_OK: {
      ret = JS_NewBool(ctx, resp->status >= 200 && resp->status <= 299);
      break;
    }

    case RESPONSE_STATUSTEXT: {
      ret = resp->status_text ? JS_NewString(ctx, resp->status_text) : JS_NULL;
      break;
    }

    case RESPONSE_HEADERS_SENT: {
      ret = JS_NewBool(ctx, resp->headers_sent);
      break;
    }

    case RESPONSE_URL: {
      ret = minnet_url_new(ctx, resp->url);
      break;
    }

    case RESPONSE_TYPE: {
      ret = JS_NewString(ctx, resp->status == 0 ? "error" : "default");
      break;
    }

    case RESPONSE_HEADERS: {
      ret = headers_object(ctx, resp->headers.start, resp->headers.end);
      // ret = minnet_headers_wrap(ctx, &resp->headers, response_dup(resp), (HeadersFreeFunc*)&response_free);
      break;
    }

    case RESPONSE_BODYUSED: {
      ret = JS_NewBool(ctx, response_body_used(resp));
      break;
    }

    case RESPONSE_BODY: {
      if(resp->body)
        // ret = minnet_generator_wrap(ctx, generator_dup(resp->body));
        ret = minnet_generator_iterator(ctx, generator_dup(resp->body));
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

    case RESPONSE_STATUSTEXT: {
      if(resp->status_text)
        js_free(ctx, resp->status_text);

      resp->status_text = js_tostring(ctx, value);
      break;
    }

    case RESPONSE_HEADERS_SENT: {
      resp->headers_sent = JS_ToBool(ctx, value);
      break;
    }

    case RESPONSE_URL: {
      url_free(&resp->url, JS_GetRuntime(ctx));
      url_parse(&resp->url, str, ctx);
      break;
    }

    case RESPONSE_HEADERS: {
      buffer_reset(&resp->headers);
      headers_fromobj(&resp->headers, value, "\n", ": ", ctx);
      break;
    }

    case RESPONSE_BODYUSED: {
      break;
    }
  }

  JS_FreeCString(ctx, str);
  return ret;
}

static JSValue
minnet_response_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_UNDEFINED;
  MinnetResponse* resp;

  if(!(resp = minnet_response_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(resp->body)
    ret = minnet_generator_create(ctx, &resp->body);

  return ret;
}

JSValue
minnet_response_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetResponse* resp;
  int i;

  if(!(resp = js_malloc(ctx, sizeof(MinnetResponse))))
    return JS_EXCEPTION;

  response_zero(resp);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_response_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_response_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, resp);

  if(argc > 0) {
    Generator* generator = response_generator(resp, ctx);

    assert(generator);

    generator_enqueue(generator, argv[0]);
    queue_close(generator->q);

    if(argc > 1)
      js_copy_properties(ctx, obj, argv[1], JS_GPN_STRING_MASK);
  }

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
    response_free(res, rt);
}

static const JSClassDef minnet_response_class = {
    "MinnetResponse",
    .finalizer = minnet_response_finalizer,
};

static const JSCFunctionListEntry minnet_response_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("append", 2, minnet_response_header, RESPONSE_HEADERS_APPEND),
    JS_CFUNC_MAGIC_DEF("get", 1, minnet_response_header, RESPONSE_HEADERS_GET),
    JS_CFUNC_MAGIC_DEF("json", 0, minnet_response_method, RESPONSE_JSON),
    JS_CFUNC_MAGIC_DEF("location", 1, minnet_response_header, RESPONSE_HEADERS_LOCATION),
    JS_CFUNC_MAGIC_DEF("set", 2, minnet_response_header, RESPONSE_HEADERS_SET),
    JS_CFUNC_MAGIC_DEF("text", 0, minnet_response_method, RESPONSE_TEXT),
    JS_CFUNC_MAGIC_DEF("write", 1, minnet_response_method, RESPONSE_WRITE),
    JS_CFUNC_MAGIC_DEF("finish", 0, minnet_response_method, RESPONSE_FINISH),
    JS_CFUNC_MAGIC_DEF("arrayBuffer", 0, minnet_response_method, RESPONSE_ARRAYBUFFER),
    JS_CFUNC_DEF("clone", 0, minnet_response_clone),
    JS_CFUNC_DEF("[Symbol.asyncIterator]", 0, minnet_response_iterator),
    JS_CGETSET_MAGIC_FLAGS_DEF("body", minnet_response_get, minnet_response_set, RESPONSE_BODY, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("bodyUsed", minnet_response_get, 0, RESPONSE_BODYUSED, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("headers", minnet_response_get, minnet_response_set, RESPONSE_HEADERS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("headersSent", minnet_response_get, 0, RESPONSE_HEADERS_SENT),
    JS_CGETSET_MAGIC_DEF("ok", minnet_response_get, 0, RESPONSE_OK),
    JS_CGETSET_MAGIC_DEF("redirected", minnet_response_get, minnet_response_set, RESPONSE_REDIRECTED),
    JS_CGETSET_MAGIC_FLAGS_DEF("status", minnet_response_get, minnet_response_set, RESPONSE_STATUS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("statusText", minnet_response_get, minnet_response_set, RESPONSE_STATUSTEXT),
    JS_CGETSET_MAGIC_DEF("type", minnet_response_get, 0, RESPONSE_TYPE),
    JS_CGETSET_MAGIC_FLAGS_DEF("url", minnet_response_get, minnet_response_set, RESPONSE_URL, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetResponse", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry minnet_response_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("redirect", 1, minnet_response_function, RESPONSE_REDIRECT),
    JS_CFUNC_MAGIC_DEF("error", 0, minnet_response_function, RESPONSE_ERROR),
};

int
minnet_response_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&minnet_response_class_id);
  JS_NewClass(JS_GetRuntime(ctx), minnet_response_class_id, &minnet_response_class);

  minnet_response_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_response_proto, minnet_response_proto_funcs, countof(minnet_response_proto_funcs));
  JS_SetClassProto(ctx, minnet_response_class_id, minnet_response_proto);

  minnet_response_ctor = JS_NewCFunction2(ctx, minnet_response_constructor, "MinnetResponse", 0, JS_CFUNC_constructor, 0);
  JS_SetPropertyFunctionList(ctx, minnet_response_ctor, minnet_response_static_funcs, countof(minnet_response_static_funcs));
  JS_SetConstructor(ctx, minnet_response_ctor, minnet_response_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Response", minnet_response_ctor);

  return 0;
}

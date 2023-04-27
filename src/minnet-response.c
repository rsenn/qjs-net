#include "minnet-websocket.h"
#include "minnet-response.h"
#include "minnet-generator.h"
#include "minnet-headers.h"
#include "minnet.h"
#include "buffer.h"
#include "jsutils.h"
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
};

MinnetResponse*
minnet_response_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_response_class_id);
}

JSValue
minnet_response_new(JSContext* ctx, MinnetURL url, int status, char* status_text, BOOL headers_sent, const char* type) {
  MinnetResponse* resp;

  if((resp = response_new(ctx))) {
    response_init(resp, url, status, status_text, headers_sent, type ? js_strdup(ctx, type) : 0);

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
  JSValue ret;
  ResolveFunctions funcs;
  MinnetResponse* resp;
  Generator* gen;

  if(!(resp = minnet_response_data2(ctx, this_val)))
    return JS_EXCEPTION;

  ret = js_async_create(ctx, &funcs);

  gen = response_generator(resp, ctx);

  switch(magic) {
    case RESPONSE_ARRAYBUFFER: {
      generator_continuous(gen, funcs.resolve);
      break;
    }
    case RESPONSE_TEXT: {
      generator_continuous(gen, funcs.resolve);
      break;
    }
    case RESPONSE_JSON: {
      generator_continuous(gen, funcs.resolve);
      break;
    }
  }

  js_async_free(ctx, &funcs);

  return ret;
}

enum {
  HEADERS_GET = 0,
  HEADERS_SET,
  HEADERS_APPEND,
  HEADERS_LOCATION,
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
    case HEADERS_GET: {
      size_t vlen;
      char* v;

      if((v = headers_getlen(&resp->headers, &vlen, key, "\r\n", ":")))
        ret = JS_NewStringLen(ctx, v, vlen);

      break;
    }
    case HEADERS_SET: {
      const char* v;

      if((v = JS_ToCString(ctx, argv[1])))
        ret = JS_NewInt32(ctx, headers_set(&resp->headers, key, v, "\r\n"));

      break;
    }
    case HEADERS_APPEND: {
      const char* v;
      size_t vlen;

      if((v = JS_ToCStringLen(ctx, &vlen, argv[1])))
        ret = JS_NewInt32(ctx, headers_appendb(&resp->headers, key, keylen, v, vlen, "\r\n"));

      break;
    }
    case HEADERS_LOCATION: {
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
      char* type;
      size_t len;

      /*   if((type = resp->type))
           len = strlen(type);
         else*/
      type = headers_getlen(&resp->headers, &len, "content-type", "\r\n", ":");

      ret = type ? JS_NewStringLen(ctx, type, len) : JS_NULL;
      break;
    }
      /* case RESPONSE_OFFSET: {
         ret = JS_NewInt64(ctx, buffer_HEAD(resp->body));
         break;
       }*/
    case RESPONSE_HEADERS: {
      ret = minnet_headers_wrap(ctx, &resp->headers, response_dup(resp), (HeadersFreeFunc*)&response_free);
      break;
    }
    case RESPONSE_BODYUSED: {
      ret = JS_NewBool(ctx, resp->body != NULL);
      break;
    }
      /* case RESPONSE_BODY: {
         if(resp->body)
           ret = minnet_generator_iterator(ctx, generator_dup(resp->body));
         break;
       }*/
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
    case RESPONSE_HEADERS_SENT: {
      resp->headers_sent = JS_ToBool(ctx, value);
      break;
    }
    case RESPONSE_URL: {
      url_free(&resp->url, ctx);
      url_parse(&resp->url, str, ctx);
      break;
    }
    case RESPONSE_TYPE: {
      headers_set(&resp->headers, "content-type", str, "\r\n");
      //  resp->type = js_strdup(ctx, str);
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

  if(!(resp = js_mallocz(ctx, sizeof(MinnetResponse))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_response_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_response_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  if(argc >= 1 && argc < 3) {

    if(!js_is_nullish(argv[0])) {
      // XXXX buffer_fromvalue(resp->body, argv[0], ctx);
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
      JS_FreeCString(ctx, str);

    } else if(JS_IsBool(argv[i])) {
      resp->headers_sent = JS_ToBool(ctx, argv[i]);
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
    response_free(res, rt);
}

JSClassDef minnet_response_class = {
    "MinnetResponse",
    .finalizer = minnet_response_finalizer,
};
const JSCFunctionListEntry minnet_response_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("append", 2, minnet_response_header, HEADERS_APPEND),
    JS_CFUNC_MAGIC_DEF("get", 1, minnet_response_header, HEADERS_GET),
    JS_CFUNC_MAGIC_DEF("json", 0, minnet_response_method, RESPONSE_JSON),
    JS_CFUNC_MAGIC_DEF("location", 1, minnet_response_header, HEADERS_LOCATION),
    JS_CFUNC_MAGIC_DEF("set", 2, minnet_response_header, HEADERS_SET),
    JS_CFUNC_MAGIC_DEF("text", 0, minnet_response_method, RESPONSE_TEXT),

    JS_CFUNC_DEF("clone", 0, minnet_response_clone),
    JS_CFUNC_DEF("[Symbol.asyncIterator]", 0, minnet_response_iterator),

    // JS_CGETSET_MAGIC_FLAGS_DEF("body", minnet_response_get, minnet_response_set, RESPONSE_BODY, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("bodyUsed", minnet_response_get, 0, RESPONSE_BODYUSED, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("headers", minnet_response_get, 0, RESPONSE_HEADERS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("headersSent", minnet_response_get, 0, RESPONSE_HEADERS_SENT),
    JS_CGETSET_MAGIC_DEF("ok", minnet_response_get, 0, RESPONSE_OK),
    JS_CGETSET_MAGIC_DEF("redirected", minnet_response_get, minnet_response_set, RESPONSE_REDIRECTED),
    JS_CGETSET_MAGIC_FLAGS_DEF("status", minnet_response_get, minnet_response_set, RESPONSE_STATUS, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("statusText", minnet_response_get, minnet_response_set, RESPONSE_STATUSTEXT),
    JS_CGETSET_MAGIC_DEF("type", minnet_response_get, minnet_response_set, RESPONSE_TYPE),
    JS_CGETSET_MAGIC_FLAGS_DEF("url", minnet_response_get, minnet_response_set, RESPONSE_URL, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetResponse", JS_PROP_CONFIGURABLE),
};

const size_t minnet_response_proto_funcs_size = countof(minnet_response_proto_funcs);

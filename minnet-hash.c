#define _GNU_SOURCE
#include "minnet.h"
#include "minnet-hash.h"

THREAD_LOCAL JSClassID minnet_hash_class_id;
THREAD_LOCAL JSValue minnet_hash_proto, minnet_hash_ctor;

enum { HASH_VALUEOF, HASH_TOSTRING, HASH_TYPE, HASH_SIZE };

static char hash_hexdigits[] = "0123456789abcdef";

MinnetHash*
hash_alloc(JSContext* ctx, enum lws_genhash_types type) {
  MinnetHash* ret;
  size_t bytes = lws_genhash_size(type);

  ret = js_mallocz(ctx, sizeof(MinnetHash) + bytes);
  ret->ref_count = 1;
  ret->size = bytes;
  ret->type = type;
  return ret;
}

void
hash_free(MinnetHash* h, JSContext* ctx) {
  if(--h->ref_count == 0)
    js_free(ctx, h);
}

void
hash_free_rt(MinnetHash* h, JSRuntime* rt) {
  if(--h->ref_count == 0)
    js_free_rt(rt, h);
}

JSValue
minnet_hash_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetHash* h = NULL;
  int32_t type = -1;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_hash_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_hash_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  if(argc >= 1)
    JS_ToInt32(ctx, &type, argv[0]);

  if(type < LWS_GENHASH_TYPE_MD5 || type > LWS_GENHASH_TYPE_SHA256) {
    JS_ThrowInternalError(ctx, "argument 1 must be one of Hash.TYPE_*");
    goto fail;
  }

  if(!(h = hash_alloc(ctx, type))) {
    JS_ThrowOutOfMemory(ctx);
    goto fail;
  }

  if(lws_genhash_init(&h->lws, type)) {
    JS_ThrowInternalError(ctx, "failed to initialize lws_genhash_ctx");
    goto fail;
  }

  JS_SetOpaque(obj, h);

  return obj;

fail:
  if(h)
    js_free(ctx, h);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
minnet_hash_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MinnetHash* h;
  JSValue ret = JS_UNDEFINED;

  if(!(h = minnet_hash_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case HASH_VALUEOF: {
      ret = JS_NewArrayBufferCopy(ctx, h->digest, h->size);
      break;
    }
    case HASH_TOSTRING: {
      unsigned i;
      char buf[h->size * 2];
      for(i = 0; i < h->size; i++) {
        buf[(i << 1)] = hash_hexdigits[h->digest[i] >> 4];
        buf[(i << 1) + 1] = hash_hexdigits[h->digest[i] & 0x0f];
      }
      ret = JS_NewStringLen(ctx, buf, h->size * 2);
      break;
    }
  }

  return ret;
}

static JSValue
minnet_hash_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetHash* h;
  JSValue ret = JS_UNDEFINED;

  if(!(h = minnet_hash_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case HASH_TYPE: {
      ret = JS_NewInt32(ctx, h->type);
      break;
    }
    case HASH_SIZE: {
      ret = JS_NewUint32(ctx, h->size);
      break;
    }
  }
  return ret;
}

static JSValue
minnet_hash_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetHash* h;
  JSValue ret = JS_UNDEFINED;

  if(!(h = minnet_hash_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static void
minnet_hash_finalizer(JSRuntime* rt, JSValue val) {
  MinnetHash* h;

  if((h = minnet_hash_data(val)))
    hash_free_rt(h, rt);
}

JSValue
minnet_hash_call(JSContext* ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst argv[], int flags) {
  MinnetHash* h = minnet_hash_data2(ctx, func_obj);
  JSValue ret = JS_UNDEFINED;

  /* if(argc < 1)
     return JS_ThrowInternalError(ctx, "argument 1 must be String, ArrayBuffer or null");*/

  if(argc < 1 || js_is_nullish(argv[0])) {

    if(!lws_genhash_destroy(&h->lws, h->digest))
      ret = JS_NewArrayBufferCopy(ctx, h->digest, h->size);

    //  ret = JS_NewInt32(ctx, lws_spa_finalize(h->spa));

  } else {
    JSBuffer buf;

    js_buffer_from(ctx, &buf, argv[0]);

    if(buf.data == 0)
      return JS_ThrowInternalError(ctx, "argument 1 must be String or ArrayBuffer");

    ret = JS_NewInt32(ctx, lws_genhash_update(&h->lws, buf.data, buf.size));

    js_buffer_free(&buf, ctx);
  }
  return ret;
}

JSClassDef minnet_hash_class = {
    "MinnetHash",
    .finalizer = minnet_hash_finalizer,
    .call = &minnet_hash_call,
};

const JSCFunctionListEntry minnet_hash_proto_funcs[] = {
    JS_PROP_CFUNC_MAGIC_DEF("valueOf", 0, minnet_hash_method, HASH_VALUEOF),
    JS_PROP_CFUNC_MAGIC_DEF("toString", 0, minnet_hash_method, HASH_TOSTRING),
    JS_CGETSET_MAGIC_DEF("type", minnet_response_get, 0, HASH_TYPE),
    JS_CGETSET_MAGIC_DEF("size", minnet_response_get, 0, HASH_SIZE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetHash", JS_PROP_CONFIGURABLE),
};

const JSCFunctionListEntry minnet_hash_static_funcs[] = {
    JS_PROP_INT32_DEF("TYPE_MD5", LWS_GENHASH_TYPE_MD5, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_SHA1", LWS_GENHASH_TYPE_SHA1, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_SHA256", LWS_GENHASH_TYPE_SHA256, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_SHA384", LWS_GENHASH_TYPE_SHA384, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_SHA512", LWS_GENHASH_TYPE_SHA512, JS_PROP_ENUMERABLE),
};

const size_t minnet_hash_proto_funcs_size = countof(minnet_hash_proto_funcs);
const size_t minnet_hash_static_funcs_size = countof(minnet_hash_static_funcs);

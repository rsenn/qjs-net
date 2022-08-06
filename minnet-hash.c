#define _GNU_SOURCE
#include "minnet.h"
#include "minnet-hash.h"
#include <assert.h>

THREAD_LOCAL JSClassID minnet_hash_class_id;
THREAD_LOCAL JSValue minnet_hash_proto, minnet_hash_ctor;

enum {
  HASH_VALUEOF,
  HASH_TOSTRING,
  HASH_UPDATE,
  HASH_FINALIZE,
  HASH_TYPE,
  HASH_SIZE,
};

static char hash_hexdigits[] = "0123456789abcdef";

static MinnetHash*
hash_alloc(JSContext* ctx, uint8_t type, BOOL hmac) {
  MinnetHash* ret;
  size_t bytes = hmac ? lws_genhmac_size(type - (LWS_GENHASH_TYPE_SHA256 - LWS_GENHMAC_TYPE_SHA256)) : lws_genhash_size(type);

  ret = js_mallocz(ctx, sizeof(MinnetHash) + bytes);
  ret->type = type;
  ret->hmac = hmac;
  return ret;
}

static BOOL
hash_init(MinnetHash* h, const uint8_t* key, size_t key_len) {
  assert(!h->initialized);
  if(h->hmac ? lws_genhmac_init(&h->lws.hmac, h->type - (LWS_GENHASH_TYPE_SHA256 - LWS_GENHMAC_TYPE_SHA256), key, key_len) : lws_genhash_init(&h->lws.hash, h->type))
    return FALSE;
  h->initialized = TRUE;
  return TRUE;
}

static size_t
hash_size(MinnetHash* h) {
  return h->hmac ? lws_genhmac_size(h->type - (LWS_GENHASH_TYPE_SHA256 - LWS_GENHMAC_TYPE_SHA256)) : lws_genhash_size(h->type);
}

static BOOL
hash_update(MinnetHash* h, const void* data, size_t size) {
  assert(h->initialized);
  assert(!h->finalized);
  if(h->hmac ? lws_genhmac_update(&h->lws.hmac, data, size) : lws_genhash_update(&h->lws.hash, data, size))
    return FALSE;
  return TRUE;
}

static BOOL
hash_finalize(MinnetHash* h) {
  assert(h->initialized);
  assert(!h->finalized);
  if(h->hmac ? lws_genhmac_destroy(&h->lws.hmac, h->digest) : lws_genhash_destroy(&h->lws.hash, h->digest))
    return FALSE;
  h->finalized = TRUE;
  return TRUE;
}

JSValue
minnet_hash_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetHash* h = NULL;
  int32_t type = -1;
  JSBuffer buf = JS_BUFFER_DEFAULT();

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

  if(type < LWS_GENHASH_TYPE_MD5 || type > LWS_GENHASH_TYPE_SHA512) {
    JS_ThrowInternalError(ctx, "argument 1 must be one of Hash.TYPE_*");
    goto fail;
  }

  if(type >= LWS_GENHASH_TYPE_SHA256 && argc > 1) {
    buf = js_input_args(ctx, argc - 1, argv + 1);

    if(buf.data == 0) {
      JS_ThrowInternalError(ctx, "argument 2 must be String, ArrayBuffer, DataView or TypedArray");
      goto fail;
    }
  }

  if(!(h = hash_alloc(ctx, type, buf.data != NULL))) {
    JS_ThrowOutOfMemory(ctx);
    goto fail;
  }

  if(!hash_init(h, buf.data, buf.size)) {
    JS_ThrowInternalError(ctx, "failed to initialize MinnetHash");
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

  if(magic <= HASH_FINALIZE)
    if(h->finalized)
      return JS_ThrowInternalError(ctx, "Hash already finalized");

  if(magic >= HASH_VALUEOF)
    if(!h->finalized)
      return JS_ThrowInternalError(ctx, "Hash not finalized");

  switch(magic) {
    case HASH_UPDATE: {
      JSBuffer buf = js_input_args(ctx, argc, argv);

      if(buf.data == 0) {
        JS_ThrowInternalError(ctx, "argument 1 must be String, ArrayBuffer, DataView or TypedArray");
        ret = JS_EXCEPTION;
      } else if(!hash_update(h, buf.data, buf.size)) {
        JS_ThrowInternalError(ctx, "Hash update failed");
        ret = JS_EXCEPTION;
      } else {
        ret = JS_NewInt64(ctx, buf.size);
      }

      js_buffer_free(&buf, ctx);
      break;
    }
    case HASH_FINALIZE: {

      if(!hash_finalize(h))
        return JS_ThrowInternalError(ctx, "Hash finalize");

      ret = JS_NewArrayBufferCopy(ctx, h->digest, hash_size(h));
      break;
    }
    case HASH_VALUEOF: {
      ret = JS_NewArrayBufferCopy(ctx, h->digest, hash_size(h));
      break;
    }
    case HASH_TOSTRING: {
      unsigned i;
      size_t size = hash_size(h);
      char buf[size * 2];
      for(i = 0; i < size; i++) {
        buf[(i << 1)] = hash_hexdigits[h->digest[i] >> 4];
        buf[(i << 1) + 1] = hash_hexdigits[h->digest[i] & 0x0f];
      }
      ret = JS_NewStringLen(ctx, buf, size * 2);
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
      ret = JS_NewUint32(ctx, hash_size(h));
      break;
    }
  }
  return ret;
}

static void
minnet_hash_finalizer(JSRuntime* rt, JSValue val) {
  MinnetHash* h;

  if((h = minnet_hash_data(val))) {
    if(!h->finalized)
      hash_finalize(h);
    js_free_rt(rt, h);
  }
}

JSValue
minnet_hash_call(JSContext* ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst argv[], int flags) {
  MinnetHash* h = minnet_hash_data2(ctx, func_obj);
  return minnet_hash_method(ctx, func_obj, argc, argv, (argc < 1 || js_is_nullish(argv[0])) ? HASH_FINALIZE : HASH_UPDATE);
}

JSClassDef minnet_hash_class = {
    "MinnetHash",
    .finalizer = minnet_hash_finalizer,
    .call = &minnet_hash_call,
};

const JSCFunctionListEntry minnet_hash_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("valueOf", 0, minnet_hash_method, HASH_VALUEOF),
    JS_CFUNC_MAGIC_DEF("toString", 0, minnet_hash_method, HASH_TOSTRING),
    JS_CFUNC_MAGIC_DEF("update", 1, minnet_hash_method, HASH_UPDATE),
    JS_CFUNC_MAGIC_DEF("finalize", 0, minnet_hash_method, HASH_FINALIZE),
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_hash_get, 0, HASH_TYPE, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("size", minnet_hash_get, 0, HASH_SIZE, 0),
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

#include "minnet.h"
#include "minnet-stream.h"
#include <quickjs.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

JSClassID minnet_stream_class_id;
JSValue minnet_stream_proto, minnet_stream_ctor;

void
stream_dump(struct stream const* strm) {
  printf("\nMinnetStream {\n\tref_count = %zu", strm->ref_count);
  buffer_dump("buffer", &strm->buffer);
  fputs("\n}", stdout);
  fflush(stdout);
}

void
stream_init(struct stream* strm, const char* type, size_t typelen, const void* x, size_t n) {
  //  memset(strm, 0, sizeof(*strm));

  if(x)
    buffer_write(&strm->buffer, x, n);

  if(type)
    pstrcpy(strm->type, MIN(typelen + 1, sizeof(strm->type)), type);
}

struct stream*
stream_new(JSContext* ctx) {
  MinnetStream* strm;

  if((strm = js_mallocz(ctx, sizeof(MinnetStream)))) {
    pstrcpy(strm->type, sizeof(strm->type), "application/binary");
  }
  return strm;
}

void
stream_zero(struct stream* strm) {
  memset(strm, 0, sizeof(MinnetStream));
  strm->buffer = BUFFER_0();
}

JSValue
minnet_stream_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetStream* strm;

  if(!(strm = js_mallocz(ctx, sizeof(MinnetStream))))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_stream_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_stream_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  while(argc >= 1) {
    size_t len;
    uint8_t* ptr;
    if(JS_IsString(argv[0])) {
      const char* str = JS_ToCStringLen(ctx, &len, argv[0]);
      buffer_append(&strm->buffer, str, len, ctx);
      JS_FreeCString(ctx, str);
    } else if((ptr = JS_GetArrayBuffer(ctx, &len, argv[0]))) {
      buffer_append(&strm->buffer, ptr, len, ctx);
    }
    argc--;
    argv++;
  }

  JS_SetOpaque(obj, strm);

  return obj;

fail:
  js_free(ctx, strm);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
minnet_stream_new(JSContext* ctx, const char* type, size_t typelen, const void* x, size_t n) {
  struct stream* strm;

  if(!(strm = stream_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  // buffer_alloc(&strm->buffer, n ? n : 1024, ctx);
  stream_init(strm, type, typelen, x, n);

  strm->ref_count = 1;

  return minnet_stream_wrap(ctx, strm);
}

JSValue
minnet_stream_wrap(JSContext* ctx, struct stream* strm) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_stream_proto, minnet_stream_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, strm);

  ++strm->ref_count;

  return ret;
}

enum { STREAM_TYPE, STREAM_LENGTH, STREAM_BUFFER, STREAM_TEXT };

static JSValue
minnet_stream_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetStream* strm;
  if(!(strm = JS_GetOpaque2(ctx, this_val, minnet_stream_class_id)))
    return JS_EXCEPTION;

  JSValue ret = JS_UNDEFINED;
  switch(magic) {

    case STREAM_TYPE: {
      ret = JS_NewStringLen(ctx, strm->type, strlen(strm->type));
      break;
    }
    case STREAM_LENGTH: {
      ret = JS_NewUint32(ctx, buffer_OFFSET(&strm->buffer));
      break;
    }
    case STREAM_BUFFER: {
      ret = buffer_OFFSET(&strm->buffer) ? buffer_toarraybuffer(&strm->buffer, ctx) : JS_NULL;
      break;
    }
    case STREAM_TEXT: {
      ret = buffer_OFFSET(&strm->buffer) ? buffer_tostring(&strm->buffer, ctx) : JS_NULL;
      break;
    }
  }
  return ret;
}

static JSValue
minnet_stream_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

static JSValue
minnet_stream_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  MinnetStream* strm;
  JSValue ret = JS_UNDEFINED;
  size_t len;
  uint8_t* ptr;

  if(!(strm = minnet_stream_data(ctx, this_val)))
    return JS_EXCEPTION;

  len = buffer_REMAIN(&strm->buffer);
  ptr = strm->buffer.read;

  if(argc >= 1) {
    uint32_t n = len;
    JS_ToUint32(ctx, &n, argv[0]);
    if(n < len)
      len = n;
  }

  if(len) {
    ret = JS_NewStringLen(ctx, (const char*)ptr, len);
    strm->buffer.read += len;
  } else {
    *pdone = TRUE;
  }

  return ret;
}

static void
minnet_stream_finalizer(JSRuntime* rt, JSValue val) {
  MinnetStream* strm = JS_GetOpaque(val, minnet_stream_class_id);
  if(strm && --strm->ref_count == 0) {

    buffer_free(&strm->buffer, rt);

    js_free_rt(rt, strm);
  }
}

JSClassDef minnet_stream_class = {
    "MinnetStream",
    .finalizer = minnet_stream_finalizer,
};

const JSCFunctionListEntry minnet_stream_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_stream_get, 0, STREAM_TYPE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("length", minnet_stream_get, 0, STREAM_LENGTH, JS_PROP_ENUMERABLE),
    JS_ITERATOR_NEXT_DEF("next", 0, minnet_stream_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, minnet_stream_iterator),
    JS_CGETSET_MAGIC_FLAGS_DEF("buffer", minnet_stream_get, 0, STREAM_BUFFER, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetStream", JS_PROP_CONFIGURABLE),
};

const size_t minnet_stream_proto_funcs_size = countof(minnet_stream_proto_funcs);

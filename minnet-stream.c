#include "minnet-stream.h"

void
stream_dump(struct stream const* strm) {
  printf("\nMinnetStream {\n\turi = %s", strm->url);
  printf("\n\tpath = %s", strm->path);
  printf("\n\ttype = %s", method_name(strm->method));

  buffer_dump("header", &strm->header);
  fputs("\n\tresponse = ", stdout);
  fputs(" }", stdout);
  fflush(stdout);
}

void
stream_init(struct stream* strm, const void* x, size_t n) {
  memset(strm, 0, sizeof(*strm));

  buffer_alloc(&strm->buffer, n);
  buffer_write(&strm->buffer, x, n);
}

struct stream*
stream_new(JSContext* ctx) {
  MinnetStream* strm;

  if((strm = js_mallocz(ctx, sizeof(MinnetStream)))) {
    buffer_alloc(&strm->buffer, 1024);
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

  if(argc >= 1) {
    if(JS_IsString(argv[0])) {
      const char* str = JS_ToCString(ctx, argv[0]);
      strm->url = js_strdup(ctx, str);
      JS_FreeCString(ctx, str);
    }
    argc--;
    argv++;
  }

  if(argc >= 1) {
    if(JS_IsObject(argv[0]) && !JS_IsNull(argv[0])) {
      js_copy_properties(ctx, obj, argv[0], JS_GPN_STRING_MASK);
      argc--;
      argv++;
    }
  }

  strm->read_only = TRUE;

  JS_SetOpaque(obj, strm);

  return obj;

fail:
  js_free(ctx, strm);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
minnet_stream_new(JSContext* ctx, const void* x, size_t n) {
  struct stream* strm;

  if(!(strm = stream_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  stream_init(strm, x, n);

  strm->method = method;

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

static JSValue
minnet_stream_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetStream* strm;
  if(!(strm = JS_GetOpaque2(ctx, this_val, minnet_stream_class_id)))
    return JS_EXCEPTION;

  JSValue ret = JS_UNDEFINED;
  switch(magic) {

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
minnet_stream_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetStream* strm;
  JSValue ret = JS_UNDEFINED;
  const char* str;
  size_t len;
  if(!(strm = JS_GetOpaque2(ctx, this_val, minnet_stream_class_id)))
    return JS_EXCEPTION;

  if(strm->read_only)
    return JS_ThrowReferenceError(ctx, "Stream object is read-only");

  str = JS_ToCStringLen(ctx, &len, value);

  switch(magic) {

    case STREAM_TEXT:
    case STREAM_BUFFER: {

      break;
    }
  }

  JS_FreeCString(ctx, str);

  return ret;
}

static void
minnet_stream_finalizer(JSRuntime* rt, JSValue val) {
  MinnetStream* strm = JS_GetOpaque(val, minnet_stream_class_id);
  if(strm && --strm->ref_count == 0) {
    if(strm->url)
      js_free_rt(rt, strm->url);

    js_free_rt(rt, strm);
  }
}

JSClassDef minnet_stream_class = {
    "MinnetStream",
    .finalizer = minnet_stream_finalizer,
};

const JSCFunctionListEntry minnet_stream_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("buffer", minnet_stream_get, minnet_stream_set, STREAM_BUFFER, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetStream", JS_PROP_CONFIGURABLE),
};

const size_t minnet_stream_proto_funcs_size = countof(minnet_stream_proto_funcs);

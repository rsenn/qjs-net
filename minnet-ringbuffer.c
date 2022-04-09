#include "minnet-ringbuffer.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>

#define MIN(asynciterator_pop, b) ((asynciterator_pop) < (b) ? (asynciterator_pop) : (b))

THREAD_LOCAL JSClassID minnet_ringbuffer_class_id;
THREAD_LOCAL JSValue minnet_ringbuffer_proto, minnet_ringbuffer_ctor;

void
ringbuffer_dump(struct ringbuffer const* strm) {
  /*  fprintf(stderr, "\nMinnetRingbuffer {\n\tref_count = %zu", strm->ref_count);
    buffer_dump("buffer", &strm->buffer);
    fputs("\n}", stderr);
    fflush(stderr);*/
}

static void
ringbuffer_destroy_element(void* element) {}

void
ringbuffer_init(struct ringbuffer* strm, size_t element_len, size_t count, const char* type, size_t typelen) {
  //  memset(strm, 0, sizeof(*strm));

  if(type)
    pstrcpy(strm->type, MIN(typelen + 1, sizeof(strm->type)), type);

  strm->ring = lws_ring_create(element_len, count, ringbuffer_destroy_element);
}

struct ringbuffer*
ringbuffer_new(JSContext* ctx) {
  struct ringbuffer* strm;

  if((strm = js_mallocz(ctx, sizeof(MinnetRingbuffer))))
    strm->ref_count = 1;
  return strm;
}

struct ringbuffer*
ringbuffer_new2(size_t element_len, size_t count, JSContext* ctx) {
  MinnetRingbuffer* strm;

  if((strm = ringbuffer_new(ctx))) {
    const char* type = "application/binary";
    ringbuffer_init(strm, element_len, count, type, strlen(type));
  }
  return strm;
}

size_t
ringbuffer_insert(struct ringbuffer* strm, const void* ptr, size_t n) {
  assert(strm->ring);

  return lws_ring_insert(strm->ring, ptr, n);
}

size_t
ringbuffer_consume(struct ringbuffer* strm, void* ptr, size_t n) {
  assert(strm->ring);

  return lws_ring_consume(strm->ring, 0, ptr, n);
}

size_t
ringbuffer_skip(struct ringbuffer* strm, size_t n) {
  assert(strm->ring);

  return lws_ring_consume(strm->ring, 0, 0, n);
}

const void*
ringbuffer_next(struct ringbuffer* strm) {
  assert(strm->ring);
  return lws_ring_get_element(strm->ring, 0);
}

size_t
ringbuffer_size(struct ringbuffer* strm) {
  assert(strm->ring);
  return lws_ring_get_count_waiting_elements(strm->ring, 0);
}

size_t
ringbuffer_avail(struct ringbuffer* strm) {
  assert(strm->ring);
  return lws_ring_get_count_free_elements(strm->ring);
}

void
ringbuffer_zero(struct ringbuffer* strm) {
  lws_ring_destroy(strm->ring);
  memset(strm, 0, sizeof(MinnetRingbuffer));
}

void
ringbuffer_free(struct ringbuffer* strm, JSRuntime* rt) {
  lws_ring_destroy(strm->ring);
  js_free_rt(rt, strm);
}

JSValue
minnet_ringbuffer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetRingbuffer* strm;

  if(!(strm = ringbuffer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_ringbuffer_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_ringbuffer_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  while(argc > 0) {

    if(JS_IsString(argv[0])) {
      const char* type;
      type = JS_ToCString(ctx, argv[0]);
      pstrcpy(strm->type, sizeof(strm->type), type);
      JS_FreeCString(ctx, type);
      argc -= 1;
      argv += 1;

    } else if(argc >= 2 && JS_IsNumber(argv[0]) && JS_IsNumber(argv[1])) {
      uint32_t element_size = 0, count = 0;
      JS_ToUint32(ctx, &element_size, argv[0]);
      JS_ToUint32(ctx, &count, argv[1]);

      strm->ring = lws_ring_create(element_size, count, ringbuffer_destroy_element);

      argc -= 2;
      argv += 2;
    } else {
      break;
    }
  }

  JS_SetOpaque(obj, strm);

  return obj;

fail:
  js_free(ctx, strm);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
minnet_ringbuffer_new(JSContext* ctx, const char* type, size_t typelen, const void* x, size_t n) {
  struct ringbuffer* strm;

  if(!(strm = ringbuffer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  // buffer_alloc(&strm->buffer, n ? n : 1024, ctx);
  // ringbuffer_init(strm, x,n, type, typelen);

  return minnet_ringbuffer_wrap(ctx, strm);
}

JSValue
minnet_ringbuffer_wrap(JSContext* ctx, struct ringbuffer* strm) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_ringbuffer_proto, minnet_ringbuffer_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, strm);

  ++strm->ref_count;

  return ret;
}

enum { RINGBUFFER_TYPE, RINGBUFFER_LENGTH, RINGBUFFER_AVAIL, RINGBUFFER_BUFFER, RINGBUFFER_TEXT };

static JSValue
minnet_ringbuffer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetRingbuffer* strm;
  if(!(strm = JS_GetOpaque2(ctx, this_val, minnet_ringbuffer_class_id)))
    return JS_EXCEPTION;

  JSValue ret = JS_UNDEFINED;
  switch(magic) {

    case RINGBUFFER_TYPE: {
      ret = JS_NewStringLen(ctx, strm->type, strlen(strm->type));
      break;
    }
    case RINGBUFFER_LENGTH: {
      ret = JS_NewUint32(ctx, lws_ring_get_count_waiting_elements(strm->ring, 0));
      break;
    }
    case RINGBUFFER_AVAIL: {
      ret = JS_NewUint32(ctx, lws_ring_get_count_free_elements(strm->ring));
      break;
    }
      /*    case RINGBUFFER_BUFFER: {
            ret = buffer_HEAD(&strm->buffer) ? buffer_toarraybuffer(&strm->buffer, ctx) : JS_NULL;
            break;
          }
          case RINGBUFFER_TEXT: {
            ret = buffer_HEAD(&strm->buffer) ? buffer_tostring(&strm->buffer, ctx) : JS_NULL;
            break;
          }*/
  }
  return ret;
}

static JSValue
minnet_ringbuffer_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

static JSValue
minnet_ringbuffer_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  MinnetRingbuffer* strm;
  JSValue ret = JS_UNDEFINED;
  size_t len;
  uint8_t* ptr;

  if(!(strm = minnet_ringbuffer_data(ctx, this_val)))
    return JS_EXCEPTION;
  /*
    len = buffer_BYTES(&strm->buffer);
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
    }*/

  return ret;
}

static void
minnet_ringbuffer_finalizer(JSRuntime* rt, JSValue val) {
  MinnetRingbuffer* strm = JS_GetOpaque(val, minnet_ringbuffer_class_id);
  if(strm && --strm->ref_count == 0) {

    // buffer_free(&strm->buffer, rt);

    js_free_rt(rt, strm);
  }
}

JSClassDef minnet_ringbuffer_class = {
    "MinnetRingbuffer",
    .finalizer = minnet_ringbuffer_finalizer,
};

const JSCFunctionListEntry minnet_ringbuffer_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_ringbuffer_get, 0, RINGBUFFER_TYPE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("length", minnet_ringbuffer_get, 0, RINGBUFFER_LENGTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("avail", minnet_ringbuffer_get, 0, RINGBUFFER_AVAIL, JS_PROP_ENUMERABLE),
    JS_ITERATOR_NEXT_DEF("next", 0, minnet_ringbuffer_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, minnet_ringbuffer_iterator),
    JS_CGETSET_MAGIC_FLAGS_DEF("buffer", minnet_ringbuffer_get, 0, RINGBUFFER_BUFFER, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetRingbuffer", JS_PROP_CONFIGURABLE),
};

const size_t minnet_ringbuffer_proto_funcs_size = countof(minnet_ringbuffer_proto_funcs);

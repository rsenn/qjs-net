#include "minnet-ringbuffer.h"
#include "jsutils.h"
#include "deferred.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>
#include <pthread.h>

THREAD_LOCAL JSClassID minnet_ringbuffer_class_id;
THREAD_LOCAL JSValue minnet_ringbuffer_proto, minnet_ringbuffer_ctor;

enum {
  RINGBUFFER_WAITING_ELEMENTS,
  RINGBUFFER_GET_ELEMENT,
  RINGBUFFER_CONSUME,
  RINGBUFFER_SKIP,
  RINGBUFFER_UPDATE_OLDEST_TAIL,
  RINGBUFFER_CREATE_TAIL,
  RINGBUFFER_INSERT,
  RINGBUFFER_BUMP_HEAD,
  RINGBUFFER_TYPE,
  RINGBUFFER_COUNT,
  RINGBUFFER_BYTELEN,
  RINGBUFFER_SIZE,
  RINGBUFFER_ELEMENTLEN,
  RINGBUFFER_AVAIL,
  RINGBUFFER_BUFFER,
  RINGBUFFER_HEAD,
  RINGBUFFER_OLDEST_TAIL,
  RINGBUFFER_INSERTRANGE,
  RINGBUFFER_CONSUMERANGE,
};

JSValue
minnet_ringbuffer_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetRingbuffer* rb;

  if(!(rb = ringbuffer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto) || JS_IsUndefined(proto))
    proto = JS_DupValue(ctx, minnet_ringbuffer_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_ringbuffer_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  while(argc > 0) {

    if(JS_IsString(argv[0])) {
      const char* type;
      type = JS_ToCString(ctx, argv[0]);
      pstrcpy(rb->type, sizeof(rb->type), type);
      JS_FreeCString(ctx, type);
      argc -= 1;
      argv += 1;

    } else if(argc >= 2 && JS_IsNumber(argv[0]) && JS_IsNumber(argv[1])) {
      uint32_t element_size = 0, count = 0;
      JS_ToUint32(ctx, &element_size, argv[0]);
      JS_ToUint32(ctx, &count, argv[1]);

      rb->ring = lws_ring_create(rb->element_len = element_size, rb->size = count, ringbuffer_destroy_element);

      argc -= 2;
      argv += 2;
    } else {
      break;
    }
  }

  JS_SetOpaque(obj, rb);

  return obj;

fail:
  js_free(ctx, rb);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

/*JSValue
minnet_ringbuffer_wrap(JSContext* ctx, struct ringbuffer* rb) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_ringbuffer_proto, minnet_ringbuffer_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, rb);

  ++rb->ref_count;

  return ret;
}*/

struct ringbuffer_tail {
  union {
    JSBuffer buf;
    uint32_t* ptr;
  };
  MinnetRingbuffer* rb;
  JSContext* ctx;
};

static void
tail_finalize(void* ptr) {
  struct ringbuffer_tail* tail = ptr;

  ringbuffer_free(tail->rb, tail->ctx);
  js_buffer_free(&tail->buf, tail->ctx);
  js_free(tail->ctx, tail);
}

static struct ringbuffer_tail*
tail_new(JSContext* ctx, struct ringbuffer* rb, JSValueConst tail_value) {
  struct ringbuffer_tail* tail;

  if((tail = js_malloc(ctx, sizeof(struct ringbuffer_tail)))) {
    tail->ctx = ctx;
    tail->rb = ringbuffer_dup(rb);
    tail->buf = js_input_chars(ctx, tail_value);
  }
  return tail;
}

static JSValue
tail_consume(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  JSValue ret = JS_UNDEFINED;
  struct ringbuffer_tail* tail = opaque;
  struct lws_ring* r = tail->rb->ring;
  uint32_t wanted;
  JSBuffer buf;

  int index = js_buffer_fromargs(ctx, argc, argv, &buf);

  js_input_args(ctx, argc, argv);

  if(buf.size) {
    wanted = buf.size / ringbuffer_element_len(tail->rb);
  } else {
    if(index == argc || JS_ToUint32(ctx, &wanted, argv[index]))
      return JS_ThrowRangeError(ctx, "need buffer or element count");
  }

  ret = JS_NewUint32(ctx, lws_ring_consume(r, tail->ptr, buf.data, wanted));

  js_buffer_free(&buf, ctx);

  return ret;
}

static JSValue
tail_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  JSValue ret = JS_UNDEFINED;
  struct ringbuffer_tail* tail = opaque;
  struct lws_ring* r = tail->rb->ring;
  uint32_t consumed, nelem = lws_ring_get_count_waiting_elements(r, tail->ptr);

  JSBuffer buf = js_buffer_alloc(ctx, nelem * ringbuffer_element_len(tail->rb));

  if((consumed = lws_ring_consume(r, tail->ptr, buf.data, nelem)) == nelem) {
    lws_ring_update_oldest_tail(r, *tail->ptr);

    ret = js_iterator_result(ctx, buf.value, FALSE);
  }

  js_buffer_free(&buf, ctx);

  return ret;
}

static JSValue
minnet_ringbuffer_multitail(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MinnetRingbuffer* rb;
  JSValue ret = JS_UNDEFINED;
  JSBuffer tail_buf;
  uint32_t new_tail, *tail_ptr = 0;
  int index = 0;

  if(!(rb = JS_GetOpaque2(ctx, this_val, minnet_ringbuffer_class_id)))
    return JS_EXCEPTION;

  index += js_buffer_fromargs(ctx, argc, argv, &tail_buf);

  if(tail_buf.data) {
    if(tail_buf.size < sizeof(uint32_t)) {
      js_buffer_free(&tail_buf, ctx);
      ret = JS_ThrowRangeError(ctx, "invalid tail");
      goto fail;
    }

    tail_ptr = (uint32_t*)tail_buf.data;
    new_tail = *tail_ptr;

  } else if(argc < 1 || JS_ToUint32(ctx, &new_tail, argv[index++])) {
    ret = JS_ThrowRangeError(ctx, "invalid tail");
    goto fail;
  }

  switch(magic) {
    case RINGBUFFER_WAITING_ELEMENTS: {
      ret = JS_NewUint32(ctx, lws_ring_get_count_waiting_elements(rb->ring, &new_tail));
      break;
    }

    case RINGBUFFER_GET_ELEMENT: {
      ret =
          JS_NewArrayBuffer(ctx, (uint8_t*)lws_ring_get_element(rb->ring, &new_tail), ringbuffer_element_len(rb), &deferred_finalizer, deferred_new(&ringbuffer_free, ringbuffer_dup(rb), ctx), FALSE);
      break;
    }

    case RINGBUFFER_CONSUME: {
      JSBuffer buf;
      uint32_t count;

      index += js_buffer_fromargs(ctx, argc - index, argv + index, &buf);

      if(buf.data) {
        size_t elem_len = ringbuffer_element_len(rb);
        if((buf.size % elem_len) != 0) {
          ret = JS_ThrowRangeError(ctx, "buffer size not a multiple of element length (%zu)", elem_len);
          break;
        }
        count = buf.size / elem_len;
      } else if(argc - index < 1 || JS_ToUint32(ctx, &count, argv[index++])) {
        ret = JS_ThrowRangeError(ctx, "invalid tail");
        break;
      }
      ret = JS_NewUint32(ctx, lws_ring_consume(rb->ring, tail_ptr, buf.data, count));
      js_buffer_free(&buf, ctx);
      break;
    }

    case RINGBUFFER_CONSUMERANGE: {
      JSValue ab = JS_GetPropertyStr(ctx, this_val, "buffer");

      ret = js_typedarray_new(ctx, 8, FALSE, FALSE, ab, new_tail, (ringbuffer_bytelength(rb) - new_tail) / rb->element_len);
      JS_FreeValue(ctx, ab);

      break;
    }
    case RINGBUFFER_SKIP: {
      uint32_t n;

      if(argc - index < 1 || JS_ToUint32(ctx, &n, argv[index])) {
        ret = JS_ThrowRangeError(ctx, "invalid count");
        break;
      }
      ret = JS_NewUint32(ctx, lws_ring_consume(rb->ring, tail_ptr, 0, n));
      break;
    }

    case RINGBUFFER_UPDATE_OLDEST_TAIL: {
      lws_ring_update_oldest_tail(rb->ring, new_tail);
      break;
    }
  }

fail:
  js_buffer_free(&tail_buf, ctx);
  return ret;
}

/*static JSValue
tail_bind(JSContext* ctx, JSValue func, JSValueConst this_val, JSValueConst arg) {
  JSValue ret = js_function_bind_this_1(ctx, func, this_val, arg);
  JS_FreeValue(ctx, func);
  return ret;
}*/

static void
tail_decorate(JSContext* ctx, JSValueConst obj, JSValueConst ringbuffer, const char* name, int argc, int magic) {
  JSValue func = JS_NewCFunctionMagic(ctx, minnet_ringbuffer_multitail, name, 0, JS_CFUNC_generic_magic, magic);
  JS_SetPropertyStr(ctx, obj, name, js_function_bind_this_1(ctx, func, ringbuffer, obj));
  JS_FreeValue(ctx, func);
}

static JSValue
minnet_ringbuffer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MinnetRingbuffer* rb;

  if(!(rb = JS_GetOpaque2(ctx, this_val, minnet_ringbuffer_class_id)))
    return JS_EXCEPTION;

  JSValue ret = JS_UNDEFINED;
  switch(magic) {

    case RINGBUFFER_CREATE_TAIL: {
      uint32_t tail = lws_ring_get_oldest_tail(rb->ring);

      ret = JS_NewArrayBufferCopy(ctx, (const uint8_t*)&tail, sizeof(uint32_t));

      tail_decorate(ctx, ret, this_val, "getWaitingElements", 0, RINGBUFFER_WAITING_ELEMENTS);
      tail_decorate(ctx, ret, this_val, "getElement", 0, RINGBUFFER_GET_ELEMENT);
      tail_decorate(ctx, ret, this_val, "consume", 1, RINGBUFFER_CONSUME);

      JS_SetPropertyStr(ctx, ret, "next", JS_NewCClosure(ctx, tail_next, 0, 0, tail_new(ctx, rb, ret), tail_finalize));
      break;
    }

    case RINGBUFFER_INSERT: {
      JSBuffer buf = js_input_args(ctx, argc, argv);
      size_t elem_len = ringbuffer_element_len(rb);

      if((buf.size % elem_len) != 0) {
        ret = JS_ThrowRangeError(ctx, "input argument size not a multiple of element length (%zu)", elem_len);
        break;
      }
      ret = JS_NewUint32(ctx, ringbuffer_insert(rb, buf.data, buf.size / elem_len));
      js_buffer_free(&buf, ctx);
      break;
    }

    case RINGBUFFER_BUMP_HEAD: {
      uint64_t n;

      if(JS_ToIndex(ctx, &n, argv[0]))
        return JS_ThrowRangeError(ctx, "expecting byte size");

      lws_ring_bump_head(rb->ring, n);
      break;
    }
  }
  return ret;
}

static JSValue
minnet_ringbuffer_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetRingbuffer* rb;

  if(!(rb = JS_GetOpaque2(ctx, this_val, minnet_ringbuffer_class_id)))
    return JS_EXCEPTION;

  JSValue ret = JS_UNDEFINED;
  switch(magic) {

    case RINGBUFFER_TYPE: {
      ret = JS_NewStringLen(ctx, rb->type, strlen(rb->type));
      break;
    }

    case RINGBUFFER_COUNT: {
      ret = JS_NewUint32(ctx, ringbuffer_waiting(rb));
      break;
    }

    case RINGBUFFER_BYTELEN: {
      ret = JS_NewInt64(ctx, ringbuffer_waiting(rb) * ringbuffer_element_len(rb));
      break;
    }

    case RINGBUFFER_SIZE: {
      ret = JS_NewUint32(ctx, rb->size);
      break;
    }

    case RINGBUFFER_ELEMENTLEN: {
      ret = JS_NewUint32(ctx, ringbuffer_element_len(rb));
      break;
    }

    case RINGBUFFER_AVAIL: {
      ret = JS_NewUint32(ctx, ringbuffer_avail(rb));
      break;
    }

    case RINGBUFFER_BUFFER: {
      struct {
        void* buf;
        void (*destroy)(void*);
        uint32_t buflen;
      }* r = (void*)rb->ring;

      ret = JS_NewArrayBuffer(ctx, r->buf, r->buflen, &deferred_finalizer, deferred_new(ringbuffer_free, ctx, ringbuffer_dup(rb)), FALSE);
      break;
    }

    case RINGBUFFER_HEAD: {
      struct {
        void* buf;
        void (*destroy_element)(void* element);
        uint32_t buflen, element_len, head, oldest_tail;
      }* r = (void*)rb->ring;

      ret = JS_NewUint32(ctx, r->head);
      break;
    }

    case RINGBUFFER_OLDEST_TAIL: {
      ret = JS_NewUint32(ctx, lws_ring_get_oldest_tail(rb->ring));
      break;
    }

    case RINGBUFFER_INSERTRANGE: {
      struct {
        void* buf;
        void (*destroy_element)(void*);
        uint32_t buflen, element_len, head;
      }* r = (void*)rb->ring;

      JSValue ab = JS_GetPropertyStr(ctx, this_val, "buffer");

      ret = js_typedarray_new(ctx, 8, FALSE, FALSE, ab, r->head, (r->buflen - r->head) / r->element_len);
      JS_FreeValue(ctx, ab);

      /*void* data = 0;
      size_t size = 0;

      if(!lws_ring_next_linear_insert_range(rb->ring, &data, &size))
        ret = JS_NewArrayBuffer(ctx, data, size, js_closure_free_ab, js_closure_new(ctx, ringbuffer_dup(rb), (js_closure_finalizer_t*)ringbuffer_free), FALSE);*/
      break;
    }
  }
  return ret;
}

static JSValue
minnet_ringbuffer_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetRingbuffer* rb;
  struct {
    void* buf;
    void (*destroy_element)(void*);
    uint32_t buflen, element_len, head;
  }* r = 0;

  if(!(rb = JS_GetOpaque2(ctx, this_val, minnet_ringbuffer_class_id)))
    return JS_EXCEPTION;

  r = (void*)rb->ring;

  JSValue ret = JS_UNDEFINED;
  switch(magic) {
    case RINGBUFFER_HEAD: {
      uint32_t n;

      if(JS_ToUint32(ctx, &n, value))
        return JS_ThrowRangeError(ctx, "expecting byte offset");

      r->head = n % r->buflen;
      break;
    }

    case RINGBUFFER_OLDEST_TAIL: {
      uint32_t n;

      if(JS_ToUint32(ctx, &n, value))
        return JS_ThrowRangeError(ctx, "expecting byte offset");

      if(n > r->buflen)
        return JS_ThrowRangeError(ctx, "out of range (0-%" PRIu32 ") byte offset", r->buflen);

      lws_ring_update_oldest_tail(rb->ring, n);
      break;
    }
  }

  return ret;
}

static void
minnet_ringbuffer_finalizer(JSRuntime* rt, JSValue val) {
  MinnetRingbuffer* rb;

  if((rb = JS_GetOpaque(val, minnet_ringbuffer_class_id))) {

    ringbuffer_free_rt(rb, rt);
  }
}

JSClassDef minnet_ringbuffer_class = {
    "MinnetRingbuffer",
    .finalizer = minnet_ringbuffer_finalizer,
};

const JSCFunctionListEntry minnet_ringbuffer_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("getWaitingElements", 0, minnet_ringbuffer_multitail, RINGBUFFER_WAITING_ELEMENTS),
    JS_CFUNC_MAGIC_DEF("getElement", 0, minnet_ringbuffer_multitail, RINGBUFFER_GET_ELEMENT),
    JS_CFUNC_MAGIC_DEF("consume", 1, minnet_ringbuffer_multitail, RINGBUFFER_CONSUME),
    JS_CFUNC_MAGIC_DEF("skip", 0, minnet_ringbuffer_multitail, RINGBUFFER_SKIP),
    JS_CFUNC_MAGIC_DEF("updateOldestTail", 0, minnet_ringbuffer_multitail, RINGBUFFER_UPDATE_OLDEST_TAIL),
    JS_CFUNC_MAGIC_DEF("getConsumeRange", 0, minnet_ringbuffer_multitail, RINGBUFFER_CONSUMERANGE),
    JS_CFUNC_MAGIC_DEF("createTail", 0, minnet_ringbuffer_method, RINGBUFFER_CREATE_TAIL),
    JS_CFUNC_MAGIC_DEF("insert", 1, minnet_ringbuffer_method, RINGBUFFER_INSERT),
    JS_CFUNC_MAGIC_DEF("bumpHead", 1, minnet_ringbuffer_method, RINGBUFFER_BUMP_HEAD),
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_ringbuffer_get, 0, RINGBUFFER_TYPE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("length", minnet_ringbuffer_get, 0, RINGBUFFER_COUNT, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("byteLength", minnet_ringbuffer_get, 0, RINGBUFFER_BYTELEN, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("size", minnet_ringbuffer_get, 0, RINGBUFFER_SIZE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("elementLength", minnet_ringbuffer_get, 0, RINGBUFFER_ELEMENTLEN, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("avail", minnet_ringbuffer_get, 0, RINGBUFFER_AVAIL, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("buffer", minnet_ringbuffer_get, 0, RINGBUFFER_BUFFER, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("head", minnet_ringbuffer_get, minnet_ringbuffer_set, RINGBUFFER_HEAD, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("oldestTail", minnet_ringbuffer_get, minnet_ringbuffer_set, RINGBUFFER_OLDEST_TAIL, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("linearInsertRange", minnet_ringbuffer_get, 0, RINGBUFFER_INSERTRANGE, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetRingbuffer", JS_PROP_CONFIGURABLE),
};

const size_t minnet_ringbuffer_proto_funcs_size = countof(minnet_ringbuffer_proto_funcs);

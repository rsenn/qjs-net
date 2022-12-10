#include "minnet-ringbuffer.h"
#include "jsutils.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>
#include <pthread.h>

THREAD_LOCAL JSClassID minnet_ringbuffer_class_id, minnet_ringbuffer_tail_class_id;
THREAD_LOCAL JSValue minnet_ringbuffer_proto, minnet_ringbuffer_tail_proto, minnet_ringbuffer_ctor;

enum {
  RINGBUFFER_TYPE,
  RINGBUFFER_LENGTH,
  RINGBUFFER_SIZE,
  RINGBUFFER_AVAIL,
  RINGBUFFER_ELEMENT_LENGTH,
  RINGBUFFER_OLDEST_TAIL,
  RINGBUFFER_WAITING_ELEMENTS,
  RINGBUFFER_BUMP_HEAD,
  RINGBUFFER_CREATE_TAIL,
  RINGBUFFER_UPDATE_OLDEST_TAIL,
  RINGBUFFER_CONSUME_AND_UPDATE_OLDEST_TAIL,
  RINGBUFFER_INSERT,
  RINGBUFFER_CONSUME,
  RINGBUFFER_SKIP,
  RINGBUFFER_BUFFER,
  RINGBUFFER_GET_ELEMENT,
  RINGBUFFER_NEXT_LINEAR_INSERT_RANGE,
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

JSValue
minnet_ringbuffer_new(JSContext* ctx, const char* type, size_t typelen, const void* x, size_t n) {
  struct ringbuffer* rb;

  if(!(rb = ringbuffer_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  // buffer_alloc(&rb->buffer, n ? n : 1024, ctx);
  // ringbuffer_init(rb, x,n, type, typelen);

  return minnet_ringbuffer_wrap(ctx, rb);
}

JSValue
minnet_ringbuffer_wrap(JSContext* ctx, struct ringbuffer* rb) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_ringbuffer_proto, minnet_ringbuffer_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, rb);

  ++rb->ref_count;

  return ret;
}

/*static void
minnet_ringbuffer_free_ab(JSRuntime* rt, void* opaque, void* ptr) {
  MinnetRingbuffer* rb = opaque;

  if(rb) {}
}*/

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
    case RINGBUFFER_LENGTH: {
      ret = JS_NewUint32(ctx, ringbuffer_size(rb));
      break;
    }
    case RINGBUFFER_SIZE: {
      ret = JS_NewUint32(ctx, rb->size);
      break;
    }
    case RINGBUFFER_ELEMENT_LENGTH: {
      ret = JS_NewUint32(ctx, ringbuffer_element_len(rb));
      break;
    }
    case RINGBUFFER_AVAIL: {
      ret = JS_NewUint32(ctx, ringbuffer_avail(rb));
      break;
    }
    case RINGBUFFER_BUFFER: {
      uint8_t* data;
      size_t size;

      if(!lws_ring_next_linear_insert_range(rb->ring, (void**)&data, &size)) {
        ret = JS_NewArrayBuffer(ctx, data, size, js_closure_free_ab, js_closure_new(ctx, ringbuffer_dup(rb), (js_closure_finalizer_t*)ringbuffer_free), FALSE);
      }
      break;
    }
    case RINGBUFFER_OLDEST_TAIL: {
      ret = JS_NewUint32(ctx, lws_ring_get_oldest_tail(rb->ring));
      break;
    }
  }
  return ret;
}

struct tail_closure {
  union {
    JSBuffer tail;
    uint32_t* tail_ptr;
  };
  MinnetRingbuffer* rb;
  JSContext* ctx;
};

static void
minnet_ringbuffer_tail_finalize(void* ptr) {
  struct tail_closure* closure = ptr;

  ringbuffer_free(closure->rb, closure->ctx);
  js_buffer_free(&closure->tail, closure->ctx);
  js_free(closure->ctx, closure);
}

static JSValue
minnet_ringbuffer_tail(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  JSValue ret = JS_UNDEFINED;
  struct tail_closure* closure = opaque;
  MinnetRingbuffer* rb = closure->rb;
  uint32_t *tail_ptr = closure->tail_ptr, consumed, nelem = lws_ring_get_count_waiting_elements(rb->ring, closure->tail_ptr);

  JSBuffer buf = js_buffer_alloc(ctx, nelem * ringbuffer_element_len(rb));

  if((consumed = lws_ring_consume(rb->ring, tail_ptr, buf.data, nelem)) == nelem) {
    lws_ring_update_oldest_tail(rb->ring, *tail_ptr);

    ret = js_iterator_result(ctx, buf.value, FALSE);
  }

  js_buffer_free(&buf, ctx);

  return ret;
}

static JSValue
minnet_ringbuffer_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MinnetRingbuffer* rb;
  if(!(rb = JS_GetOpaque2(ctx, this_val, minnet_ringbuffer_class_id)))
    return JS_EXCEPTION;

  JSValue ret = JS_UNDEFINED;
  switch(magic) {
    case RINGBUFFER_WAITING_ELEMENTS: {
      uint32_t* tail_ptr = 0;
      JSBuffer buf;

      if(argc > 0) {
        buf = js_input_args(ctx, argc, argv);
        if(buf.size >= sizeof(uint32_t))
          tail_ptr = (uint32_t*)buf.data;
      }

      ret = JS_NewUint32(ctx, lws_ring_get_count_waiting_elements(rb->ring, tail_ptr));

      if(tail_ptr)
        js_buffer_free(&buf, ctx);
      break;
    }
    case RINGBUFFER_GET_ELEMENT: {
      uint32_t* tail_ptr = 0;
      JSBuffer buf;

      if(argc > 0) {
        buf = js_input_args(ctx, argc, argv);
        if(buf.size >= sizeof(uint32_t))
          tail_ptr = (uint32_t*)buf.data;
      }
      {
        const void* data = lws_ring_get_element(rb->ring, tail_ptr);
        size_t size = ringbuffer_element_len(rb);

        ret = JS_NewArrayBufferCopy(ctx, data, size);
      }

      if(tail_ptr)
        js_buffer_free(&buf, ctx);
      break;
    }
    case RINGBUFFER_CREATE_TAIL: {
      uint32_t tail = lws_ring_get_oldest_tail(rb->ring);
      struct tail_closure* closure;

      ret = JS_NewArrayBufferCopy(ctx, (const uint8_t*)&tail, sizeof(uint32_t));

      if((closure = js_malloc(ctx, sizeof(struct tail_closure)))) {
        closure->ctx = ctx;
        closure->rb = ringbuffer_dup(rb);
        closure->tail = js_input_chars(ctx, ret);

        JSValue func = JS_NewCClosure(ctx, minnet_ringbuffer_tail, 0, 0, closure, minnet_ringbuffer_tail_finalize);
        JS_SetPropertyStr(ctx, ret, "next", func);
      }

      break;
    }
    case RINGBUFFER_INSERT: {
      JSBuffer data = js_input_args(ctx, argc, argv);
      size_t elem_len = ringbuffer_element_len(rb);

      // printf("data.size=%zu elem_len=%zu\n", data.size, elem_len);

      if((data.size % elem_len) != 0) {
        ret = JS_ThrowRangeError(ctx, "input argument size not a multiple of element length (%zu)", elem_len);
        break;
      }

      ret = JS_NewUint32(ctx, ringbuffer_insert(rb, data.data, data.size / elem_len));

      js_buffer_free(&data, ctx);
      break;
    }
    case RINGBUFFER_CONSUME: {
      JSBuffer data = js_input_args(ctx, argc, argv);
      size_t elem_len = ringbuffer_element_len(rb);

      // printf("data.size=%zu elem_len=%zu\n", data.size, elem_len);

      if((data.size % elem_len) != 0) {
        ret = JS_ThrowRangeError(ctx, "buffer size not a multiple of element length (%zu)", elem_len);
        break;
      }

      ret = JS_NewUint32(ctx, ringbuffer_consume(rb, data.data, data.size / elem_len));

      js_buffer_free(&data, ctx);
      break;
    }
    case RINGBUFFER_SKIP: {
      uint32_t n = 1;

      if(argc > 0)
        JS_ToUint32(ctx, &n, argv[0]);

      ret = JS_NewUint32(ctx, ringbuffer_skip(rb, n));
      break;
    }
    case RINGBUFFER_BUMP_HEAD: {
      uint64_t n;

      if(JS_ToIndex(ctx, &n, argv[0]))
        return JS_ThrowRangeError(ctx, "expecting byte size");

      lws_ring_bump_head(rb->ring, n);
      break;
    }
    case RINGBUFFER_NEXT_LINEAR_INSERT_RANGE: {
      void* data = 0;
      size_t size = 0;

      if(!lws_ring_next_linear_insert_range(rb->ring, &data, &size))
        ret = JS_NewArrayBuffer(ctx, data, size, js_closure_free_ab, js_closure_new(ctx, ringbuffer_dup(rb), (js_closure_finalizer_t*)ringbuffer_free), FALSE);
      break;
    }
  }
  return ret;
}

static JSValue
minnet_ringbuffer_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

static JSValue
minnet_ringbuffer_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  MinnetRingbuffer* rb;
  JSValue ret = JS_UNDEFINED;

  if(!(rb = minnet_ringbuffer_data(ctx, this_val)))
    return JS_EXCEPTION;
  /*
    len = buffer_REMAIN(&rb->buffer);
    ptr = rb->buffer.read;

    if(argc >= 1) {
      uint32_t n = len;
      JS_ToUint32(ctx, &n, argv[0]);
      if(n < len)
        len = n;
    }

    if(len) {
      ret = JS_NewStringLen(ctx, (const char*)ptr, len);
      rb->buffer.read += len;
    } else {
      *pdone = TRUE;
    }*/

  return ret;
}

static void
minnet_ringbuffer_finalizer(JSRuntime* rt, JSValue val) {
  MinnetRingbuffer* rb = JS_GetOpaque(val, minnet_ringbuffer_class_id);
  if(rb && --rb->ref_count == 0) {

    // buffer_free_rt(&rb->buffer, rt);

    js_free_rt(rt, rb);
  }
}

JSClassDef minnet_ringbuffer_class = {
    "MinnetRingbuffer",
    .finalizer = minnet_ringbuffer_finalizer,
};

const JSCFunctionListEntry minnet_ringbuffer_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("buffer", minnet_ringbuffer_get, 0, RINGBUFFER_BUFFER, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_ringbuffer_get, 0, RINGBUFFER_TYPE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("length", minnet_ringbuffer_get, 0, RINGBUFFER_LENGTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("size", minnet_ringbuffer_get, 0, RINGBUFFER_SIZE, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("avail", minnet_ringbuffer_get, 0, RINGBUFFER_AVAIL, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("elementLength", minnet_ringbuffer_get, 0, RINGBUFFER_ELEMENT_LENGTH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("oldestTail", minnet_ringbuffer_get, 0, RINGBUFFER_OLDEST_TAIL, JS_PROP_ENUMERABLE),
    JS_CFUNC_MAGIC_DEF("getWaitingElements", 0, minnet_ringbuffer_method, RINGBUFFER_WAITING_ELEMENTS),
    JS_CFUNC_MAGIC_DEF("getElement", 0, minnet_ringbuffer_method, RINGBUFFER_WAITING_ELEMENTS),
    JS_CFUNC_MAGIC_DEF("bumpHead", 1, minnet_ringbuffer_method, RINGBUFFER_BUMP_HEAD),
    JS_CFUNC_MAGIC_DEF("createTail", 0, minnet_ringbuffer_method, RINGBUFFER_CREATE_TAIL),
    JS_CFUNC_MAGIC_DEF("updateOldestTail", 0, minnet_ringbuffer_method, RINGBUFFER_UPDATE_OLDEST_TAIL),
    JS_CFUNC_MAGIC_DEF("consumeAndUpdateOldestTail", 0, minnet_ringbuffer_method, RINGBUFFER_CONSUME_AND_UPDATE_OLDEST_TAIL),
    JS_CFUNC_MAGIC_DEF("insert", 1, minnet_ringbuffer_method, RINGBUFFER_INSERT),
    JS_CFUNC_MAGIC_DEF("consume", 1, minnet_ringbuffer_method, RINGBUFFER_CONSUME),
    JS_CFUNC_MAGIC_DEF("skip", 0, minnet_ringbuffer_method, RINGBUFFER_SKIP),
    JS_CFUNC_MAGIC_DEF("nextLinearInsertRange", 0, minnet_ringbuffer_method, RINGBUFFER_NEXT_LINEAR_INSERT_RANGE),
    JS_ITERATOR_NEXT_DEF("next", 0, minnet_ringbuffer_next, 0),

    JS_CFUNC_DEF("[Symbol.iterator]", 0, minnet_ringbuffer_iterator),
    JS_CGETSET_MAGIC_FLAGS_DEF("buffer", minnet_ringbuffer_get, 0, RINGBUFFER_BUFFER, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetRingbuffer", JS_PROP_CONFIGURABLE),
};
/*
const JSCFunctionListEntry minnet_ringbuffer_proto_tail[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, minnet_ringbuffer_next, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("elementLength", minnet_ringbuffer_get, 0, TAIL_WAITING_ELEMENTS, JS_PROP_ENUMERABLE),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, minnet_ringbuffer_iterator),
};
*/
const size_t minnet_ringbuffer_proto_funcs_size = countof(minnet_ringbuffer_proto_funcs);

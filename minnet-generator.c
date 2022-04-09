#include "minnet-generator.h"
#include "jsutils.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>

#define MIN(asynciterator_pop, b) ((asynciterator_pop) < (b) ? (asynciterator_pop) : (b))

THREAD_LOCAL JSClassID minnet_generator_class_id;
THREAD_LOCAL JSValue minnet_generator_proto, minnet_generator_ctor;

void
generator_zero(struct generator* gen) {
  gen->ref_count = 1;
  gen->buffer = BUFFER_0();
  asynciterator_zero(&gen->iterator);
}

void
generator_free(struct generator* gen, JSRuntime* rt) {
  asynciterator_clear(&gen->iterator, rt);
  buffer_free_rt(&gen->buffer, rt);
  js_free_rt(rt, gen);
}

struct generator*
generator_new(JSContext* ctx) {
  struct generator* gen;

  if((gen = js_malloc(ctx, sizeof(MinnetGenerator)))) {
    generator_zero(gen);
    gen->iterator.ctx = ctx;
  }
  return gen;
}

JSValue
generator_next(MinnetGenerator* gen, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  ret = asynciterator_await(&gen->iterator, ctx);

  if(buffer_HEAD(&gen->buffer)) {
    JSValue value = buffer_toarraybuffer(&gen->buffer, ctx);
    gen->buffer = BUFFER_0();

    asynciterator_push(&gen->iterator, value, ctx);
  }

  return ret;
}

JSValue
minnet_generator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetGenerator* gen;

  if(!(gen = generator_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_generator_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_generator_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, gen);

  return obj;

fail:
  js_free(ctx, gen);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
minnet_generator_wrap(JSContext* ctx, struct generator* gen) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_generator_proto, minnet_generator_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, gen);

  ++gen->ref_count;

  return ret;
}

enum { GENERATOR_TYPE, GENERATOR_LENGTH, GENERATOR_AVAIL, GENERATOR_BUFFER, GENERATOR_TEXT };

static JSValue
minnet_generator_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetGenerator* gen;
  if(!(gen = JS_GetOpaque2(ctx, this_val, minnet_generator_class_id)))
    return JS_EXCEPTION;

  JSValue ret = JS_UNDEFINED;
  switch(magic) {}
  return ret;
}

static JSValue
minnet_generator_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

static JSValue
minnet_generator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  MinnetGenerator* gen;
  JSValue ret = JS_UNDEFINED;
  size_t len;
  uint8_t* ptr;

  if(!(gen = minnet_generator_data(ctx, this_val)))
    return JS_EXCEPTION;

  ret = generator_next(gen, ctx);

  return ret;
}

static void
minnet_generator_finalizer(JSRuntime* rt, JSValue val) {
  MinnetGenerator* gen;
  if((gen = JS_GetOpaque(val, minnet_generator_class_id))) {
    if(--gen->ref_count == 0) {

      buffer_free(&gen->buffer, rt);

      js_free_rt(rt, gen);
    }
  }
}

JSClassDef minnet_generator_class = {
    "MinnetGenerator",
    .finalizer = minnet_generator_finalizer,
};

const JSCFunctionListEntry minnet_generator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, minnet_generator_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, minnet_generator_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetGenerator", JS_PROP_CONFIGURABLE),
};

const size_t minnet_generator_proto_funcs_size = countof(minnet_generator_proto_funcs);

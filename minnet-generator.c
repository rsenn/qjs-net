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
generator_free(struct generator* gen) {
  if(--gen->ref_count == 0) {
    asynciterator_clear(&gen->iterator, JS_GetRuntime(gen->ctx));
    buffer_free(&gen->buffer, gen->ctx);
    js_free(gen->ctx, gen);
  }
}

struct generator*
generator_new(JSContext* ctx) {
  struct generator* gen;

  if((gen = js_malloc(ctx, sizeof(MinnetGenerator)))) {
    generator_zero(gen);
    gen->ctx = ctx;
    // gen->iterator.ctx = ctx;
  }
  return gen;
}

struct generator*
generator_dup(struct generator* gen) {
  ++gen->ref_count;
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

static JSValue
minnet_generator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, void* opaque) {
  MinnetGenerator* gen = opaque;
  JSValue ret = JS_UNDEFINED;

  ret = generator_next(gen, ctx);

  return ret;
}

JSValue
minnet_generator_wrap(JSContext* ctx, MinnetGenerator* gen) {
  JSValue ret = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, ret, "next", JS_NewCClosure(ctx, minnet_generator_next, 0, 0, generator_dup(gen), generator_free));

  return ret;
}

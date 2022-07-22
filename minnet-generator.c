#include "minnet-generator.h"
#include "jsutils.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>

#define MIN(asynciterator_shift, b) ((asynciterator_shift) < (b) ? (asynciterator_shift) : (b))

THREAD_LOCAL JSClassID minnet_generator_class_id;
THREAD_LOCAL JSValue minnet_generator_proto, minnet_generator_ctor;

void
generator_zero(struct generator* gen) {
  gen->buffer = BUFFER_0();
  asynciterator_zero(&gen->iterator);
  gen->ref_count = 0;
  gen->bytes_written = 0;
  gen->bytes_read = 0;
}

void
generator_destroy(struct generator** gen_p) {
  struct generator* gen;

  if((gen = *gen_p)) {
    if(generator_free(gen))
      *gen_p = 0;
  }
}

BOOL
generator_free(struct generator* gen) {
  if(--gen->ref_count == 0) {
    asynciterator_clear(&gen->iterator, JS_GetRuntime(gen->ctx));
    buffer_free(&gen->buffer, JS_GetRuntime(gen->ctx));
    js_free(gen->ctx, gen);
    return TRUE;
  }
  return FALSE;
}

struct generator*
generator_new(JSContext* ctx) {
  struct generator* gen;

  if((gen = js_malloc(ctx, sizeof(MinnetGenerator)))) {
    generator_zero(gen);
    gen->ctx = ctx;
    gen->ref_count = 1;
    // gen->iterator.ctx = ctx;
  }
  return gen;
}

JSValue
generator_next(MinnetGenerator* gen, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  ret = asynciterator_next(&gen->iterator, ctx);

  if(buffer_HEAD(&gen->buffer)) {
    size_t len;
    int64_t bytes;
    JSValue value = buffer_toarraybuffer_size(&gen->buffer, &len, ctx);
    gen->buffer = BUFFER_0();

    asynciterator_yield(&gen->iterator, value, ctx);
    gen->bytes_read += len;
  }

  return ret;
}

ssize_t
generator_write(MinnetGenerator* gen, const void* data, size_t len) {
  ssize_t ret = -1;
  if(list_empty(&gen->iterator.reads))
    return generator_queue(gen, data, len);

  JSValue buf = JS_NewArrayBufferCopy(gen->ctx, data, len);

  asynciterator_yield(&gen->iterator, buf, gen->ctx);
  gen->bytes_written += len;
  gen->bytes_read += len;

  return ret;
}

BOOL
generator_close(MinnetGenerator* gen, JSContext* ctx) {
  return asynciterator_stop(&gen->iterator, JS_UNDEFINED, ctx);
}

ssize_t
generator_queue(MinnetGenerator* gen, const void* data, size_t len) {
  ssize_t ret;

  if((ret = buffer_append(&gen->buffer, data, len, gen->ctx)) > 0)
    gen->bytes_written += len;

  return ret;
}

static JSValue
minnet_generator_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  return JS_DupValue(ctx, this_val);
}

static const JSCFunctionListEntry minnet_generator_funcs[1] = {
    JS_CFUNC_DEF("[Symbol.asyncIterator]", 0, minnet_generator_iterator),
};

static JSValue
minnet_generator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, void* opaque) {
  MinnetGenerator* gen = *(MinnetGenerator**)opaque;
  JSValue ret = JS_UNDEFINED;

  ret = generator_next(gen, ctx);

  return ret;
}

JSValue
minnet_generator_create(JSContext* ctx, MinnetGenerator** gen_p) {
  JSValue ret = JS_NewObject(ctx);

  if(!*gen_p)
    *gen_p = generator_new(ctx);
  else
    generator_dup(*gen_p);

  JS_SetPropertyStr(ctx, ret, "next", JS_NewCClosure(ctx, minnet_generator_next, 0, 0, gen_p, (void*)&generator_free));
  JS_SetPropertyFunctionList(ctx, ret, minnet_generator_funcs, countof(minnet_generator_funcs));

  return ret;
}
/*
JSValue
minnet_generator_wrap(JSContext* ctx, MinnetGenerator* gen) {
  JSValue ret = JS_NewObject(ctx);

    ++gen->ref_count;

  JS_SetPropertyStr(ctx, ret, "next", JS_NewCClosure(ctx, minnet_generator_next, 0, 0, gen_p, (void*)&generator_free));

  return ret;
}
*/

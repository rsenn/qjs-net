#include "generator.h"

static ssize_t push_block(Generator* gen, ByteBlock blk, JSValueConst callback);
static ssize_t push_value(Generator* gen, JSValueConst value, JSValueConst callback);

void
generator_zero(Generator* gen) {
  asynciterator_zero(&gen->iterator);
  gen->q = 0;
  gen->bytes_written = 0;
  gen->bytes_read = 0;
  gen->chunks_written = 0;
  gen->chunks_read = 0;
  gen->ref_count = 0;
}

void
generator_destroy(Generator** gen_p) {
  Generator* gen;

  if((gen = *gen_p)) {
    if(generator_free(gen))
      *gen_p = 0;
  }
}

BOOL
generator_free(Generator* gen) {
  if(--gen->ref_count == 0) {
    asynciterator_clear(&gen->iterator, JS_GetRuntime(gen->ctx));

    queue_free(gen->q, gen->ctx);

    js_free(gen->ctx, gen);
    return TRUE;
  }
  return FALSE;
}

Generator*
generator_new(JSContext* ctx) {
  Generator* gen;

  if((gen = js_malloc(ctx, sizeof(Generator)))) {
    generator_zero(gen);
    gen->ctx = ctx;
    gen->ref_count = 1;
    // gen->rbuf = ringbuffer_new2(sizeof(ByteBlock), 1024, ctx);
    gen->q = queue_new(ctx);
  }
  return gen;
}

JSValue
generator_next(Generator* gen, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  ret = asynciterator_next(&gen->iterator, ctx);

  if(queue_closed(gen->q)) {
    ByteBlock blk = queue_next(gen->q, NULL);
    JSValue chunk = block_SIZE(&blk) ? block_toarraybuffer(&blk, ctx) : JS_UNDEFINED;
    asynciterator_stop(&gen->iterator, chunk, ctx);
    JS_FreeValue(ctx, chunk);

  } else if(queue_size(gen->q)) {
    BOOL done = FALSE;
    ByteBlock blk = queue_next(gen->q, &done);
    JSValue chunk = block_SIZE(&blk) ? block_toarraybuffer(&blk, ctx) : JS_UNDEFINED;
    asynciterator_yield(&gen->iterator, chunk, ctx);
    JS_FreeValue(gen->ctx, chunk);

    gen->bytes_read += block_SIZE(&blk);
    gen->chunks_read += 1;
  }

  return ret;
}

ssize_t
generator_write(Generator* gen, const void* data, size_t len, JSValueConst callback) {
  ByteBlock blk = block_new(data, len, gen->ctx);
  JSValue chunk = block_toarraybuffer(&blk, gen->ctx);
  ssize_t ret = generator_enqueue(gen, chunk, callback) ? block_SIZE(&blk) : -1;

  JS_FreeValue(gen->ctx, chunk);

  if(ret >= 0) {
    gen->bytes_written += block_SIZE(&blk);
    gen->chunks_written += 1;
  }

  return ret;
}

JSValue
generator_push(Generator* gen, JSValueConst value) {
  ResolveFunctions funcs = {JS_NULL, JS_NULL};
  JSValue ret = js_promise_create(gen->ctx, &funcs);

  if(!generator_enqueue(gen, value, funcs.resolve)) {
    JS_FreeValue(gen->ctx, JS_Call(gen->ctx, funcs.reject, JS_UNDEFINED, 0, 0));
  }

  js_promise_free(gen->ctx, &funcs);
  return ret;
}

BOOL
generator_enqueue(Generator* gen, JSValueConst value, JSValueConst callback) {
  ssize_t ret;

  if(asynciterator_yield(&gen->iterator, value, gen->ctx)) {
    JSBuffer buf = js_input_chars(gen->ctx, value);
    ret = buf.size;
    js_buffer_free(&buf, gen->ctx);
  } else {
    if((ret = push_value(gen, value, callback)) < 0)
      return FALSE;
  }

  if(ret >= 0) {
    gen->bytes_written += ret;
    gen->chunks_written += 1;
  }

  if(JS_IsFunction(gen->ctx, callback))
    JS_FreeValue(gen->ctx, JS_Call(gen->ctx, callback, JS_UNDEFINED, 0, 0));

  return TRUE;
}

BOOL
generator_close(Generator* gen, JSValueConst callback) {
  BOOL ret = FALSE;
  QueueItem* item = 0;

  if(!queue_complete(gen->q)) {
    item = queue_close(gen->q);
    ret = TRUE;
  }

  if(asynciterator_stop(&gen->iterator, JS_UNDEFINED, gen->ctx))
    ret = TRUE;

  return ret;
}

JSValue
generator_stop(Generator* gen) {
  ResolveFunctions funcs = {JS_NULL, JS_NULL};
  JSValue ret = js_promise_create(gen->ctx, &funcs);

  if(!generator_close(gen, funcs.resolve)) {
    JS_FreeValue(gen->ctx, JS_Call(gen->ctx, funcs.reject, JS_UNDEFINED, 0, 0));
  }

  js_promise_free(gen->ctx, &funcs);
  return ret;
}

static ssize_t
push_block(Generator* gen, ByteBlock blk, JSValueConst callback) {
  QueueItem* item;
  ssize_t ret = -1;

  if((item = queue_put(gen->q, blk))) {
    ret = block_SIZE(&item->block);

    if(JS_IsFunction(gen->ctx, callback))
      item->resolve = deferred_newjs(JS_FreeValue, JS_DupValue(gen->ctx, callback), gen->ctx);
  }
  return ret;
}

static ssize_t
push_value(Generator* gen, JSValueConst value, JSValueConst callback) {
  JSBuffer buf = js_input_chars(gen->ctx, value);
  ByteBlock blk = block_new(buf.data, buf.size, gen->ctx);
  ssize_t ret;
  js_buffer_free(&buf, gen->ctx);

  if((ret = push_block(gen, blk, callback)) == -1)
    block_free(&blk, gen->ctx);

  return ret;
}

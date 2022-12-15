#include "generator.h"

static ssize_t enqueue_block(Generator* gen, ByteBlock blk, JSValueConst callback);
static ssize_t enqueue_value(Generator* gen, JSValueConst value, JSValueConst callback);

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

    if(gen->q)
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
    gen->q = 0; // queue_new(ctx);
    gen->block_fn = &block_toarraybuffer;
  }
  return gen;
}

static int
generator_dequeue(Generator* gen) {
  int i = 0;

  while(!list_empty(&gen->iterator.reads) && queue_size(gen->q) > 0) {
    BOOL done = FALSE;
    ByteBlock blk = queue_next(gen->q, &done);
    JSValue chunk = block_SIZE(&blk) ? gen->block_fn(&blk, gen->ctx) : JS_UNDEFINED;
    done ? asynciterator_stop(&gen->iterator,  gen->ctx) : asynciterator_yield(&gen->iterator, chunk, gen->ctx);
    JS_FreeValue(gen->ctx, chunk);

    if(!done)
      ++i;

    if(block_BEGIN(&blk)) {
      gen->bytes_read += block_SIZE(&blk);
      gen->chunks_read += 1;
    }
  }

  return i;
}

JSValue
generator_next(Generator* gen, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  ret = asynciterator_next(&gen->iterator, ctx);

  generator_dequeue(gen);

  return ret;
}

ssize_t
generator_write(Generator* gen, const void* data, size_t len, JSValueConst callback) {
  ByteBlock blk = block_copy(data, len, gen->ctx);
  ssize_t ret = -1, size = block_SIZE(&blk);

  if(!list_empty(&gen->iterator.reads) && !(gen->q && gen->q->continuous)) {
    JSValue chunk = gen->block_fn(&blk, gen->ctx);
    if(asynciterator_yield(&gen->iterator, chunk, gen->ctx))
      ret = size;

    JS_FreeValue(gen->ctx, chunk);
  } else {
    ret = enqueue_block(gen, blk, callback);
  }
  return ret;
}

JSValue
generator_push(Generator* gen, JSValueConst value) {
  ResolveFunctions funcs = {JS_NULL, JS_NULL};
  JSValue ret = js_promise_create(gen->ctx, &funcs);

  if(!generator_yield(gen, value, funcs.resolve)) {
    JS_FreeValue(gen->ctx, JS_Call(gen->ctx, funcs.reject, JS_UNDEFINED, 0, 0));
  }

  js_promise_free(gen->ctx, &funcs);
  return ret;
}

BOOL
generator_yield(Generator* gen, JSValueConst value, JSValueConst callback) {
  ssize_t ret;

  if(asynciterator_yield(&gen->iterator, value, gen->ctx)) {
    JSBuffer buf = js_input_chars(gen->ctx, value);
    ret = buf.size;
    js_buffer_free(&buf, gen->ctx);
  } else {
    if((ret = enqueue_value(gen, value, callback)) < 0)
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
generator_cancel(Generator* gen) {
  BOOL ret = FALSE;
  QueueItem* item = 0;

  if(!queue_complete(gen->q)) {
    item = queue_close(gen->q);
    ret = TRUE;
  }
  if(asynciterator_cancel(&gen->iterator, JS_UNDEFINED, gen->ctx))
    ret = TRUE;

  return ret;
}

BOOL
generator_close(Generator* gen, JSValueConst callback) {
  BOOL ret = FALSE;
  QueueItem* item = 0;

  if(!queue_complete(gen->q)) {
    item = queue_close(gen->q);
    ret = TRUE;
  }

  gen->iterator.closing = TRUE;

  if(asynciterator_stop(&gen->iterator, gen->ctx))
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

BOOL
generator_continuous(Generator* gen) {
  Queue* q;
  if(!(q = gen->q))
    q = gen->q = queue_new(gen->ctx);
  if(q)
    q->continuous = TRUE;
  return q != NULL;
}

static ssize_t
enqueue_block(Generator* gen, ByteBlock blk, JSValueConst callback) {
  QueueItem* item;
  ssize_t ret = -1;

  if(!gen->q)
    gen->q = queue_new(gen->ctx);

  if((item = queue_put(gen->q, blk, gen->ctx))) {
    ret = block_SIZE(&item->block);

    if(JS_IsFunction(gen->ctx, callback))
      item->resolve = deferred_newjs(JS_FreeValue, JS_DupValue(gen->ctx, callback), gen->ctx);
  }
  return ret;
}

static ssize_t
enqueue_value(Generator* gen, JSValueConst value, JSValueConst callback) {
  JSBuffer buf = js_input_chars(gen->ctx, value);
  ByteBlock blk = block_copy(buf.data, buf.size, gen->ctx);
  ssize_t ret;
  js_buffer_free(&buf, gen->ctx);

  if((ret = enqueue_block(gen, blk, callback)) == -1)
    block_free(&blk, gen->ctx);

  return ret;
}

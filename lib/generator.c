#include "generator.h"

void
generator_zero(struct generator* gen) {
  asynciterator_zero(&gen->iterator);
  gen->rbuf = 0;
  gen->bytes_written = 0;
  gen->bytes_read = 0;
  gen->chunks_written = 0;
  gen->chunks_read = 0;
  gen->ref_count = 0;
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

    while(ringbuffer_size(gen->rbuf)) {
      ByteBlock blk;
      if(ringbuffer_consume(gen->rbuf, &blk, 1))
        block_free(&blk, gen->ctx);
    }

    ringbuffer_free(gen->rbuf, JS_GetRuntime(gen->ctx));
    js_free(gen->ctx, gen);
    return TRUE;
  }
  return FALSE;
}

struct generator*
generator_new(JSContext* ctx) {
  struct generator* gen;

  if((gen = js_malloc(ctx, sizeof(struct generator)))) {
    generator_zero(gen);
    gen->ctx = ctx;
    gen->ref_count = 1;
    gen->rbuf = ringbuffer_new2(sizeof(ByteBlock), 1024, ctx);
    // gen->iterator.ctx = ctx;
  }
  return gen;
}

JSValue
generator_next(struct generator* gen, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  ret = asynciterator_next(&gen->iterator, ctx);

  if(ringbuffer_size(gen->rbuf)) {
    ByteBlock blk;
    if(ringbuffer_consume(gen->rbuf, &blk, 1)) {
      JSValue value = block_toarraybuffer(&blk, ctx);

      asynciterator_yield(&gen->iterator, value, ctx);
      gen->bytes_read += block_SIZE(&blk);
      gen->chunks_read += 1;
    }
  }
  /*if(buffer_HEAD(&gen->buffer)) {
    size_t len;
    JSValue value = buffer_toarraybuffer_size(&gen->buffer, &len, ctx);
    gen->buffer = BUFFER_0();

    asynciterator_yield(&gen->iterator, value, ctx);
  }*/

  return ret;
}

ssize_t
generator_write(struct generator* gen, const void* data, size_t len) {
  ssize_t ret = -1;

  if(list_empty(&gen->iterator.reads))
    return generator_queue(gen, data, len);

  JSValue buf = JS_NewArrayBufferCopy(gen->ctx, data, len);

  asynciterator_yield(&gen->iterator, buf, gen->ctx);
  gen->bytes_written += len;
  gen->bytes_read += len;
  gen->chunks_written += 1;
  gen->chunks_read += 1;

  return ret;
}

BOOL
generator_close(struct generator* gen) {
  return asynciterator_stop(&gen->iterator, JS_UNDEFINED, gen->ctx);
}

ssize_t
generator_queue(struct generator* gen, const void* data, size_t len) {
  ssize_t ret;

  ByteBlock blk = block_copy(data, len, gen->ctx);

  ret = ringbuffer_insert(gen->rbuf, &blk, 1) ? len : 0;

  return ret;
}

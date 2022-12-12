#include "generator.h"

void
generator_zero(Generator* gen) {
  asynciterator_zero(&gen->iterator);
  gen->rbuf = 0;
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

    while(ringbuffer_waiting(gen->rbuf)) {
      ByteBlock blk;
      if(ringbuffer_consume(gen->rbuf, &blk, 1))
        block_free(&blk, gen->ctx);
    }

    ringbuffer_free_rt(gen->rbuf, JS_GetRuntime(gen->ctx));
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
    gen->rbuf = ringbuffer_new2(sizeof(ByteBlock), 1024, ctx);
  }
  return gen;
}

JSValue
generator_next(Generator* gen, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  ret = asynciterator_next(&gen->iterator, ctx);

  if(ringbuffer_waiting(gen->rbuf)) {
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
generator_write(Generator* gen, const void* data, size_t len) {
  if(list_empty(&gen->iterator.reads))
    return generator_queue(gen, data, len);

  JSValue buf = JS_NewArrayBufferCopy(gen->ctx, data, len);

  asynciterator_yield(&gen->iterator, buf, gen->ctx);
  gen->bytes_written += len;
  gen->chunks_written += 1;

  return len;
}

BOOL
generator_close(Generator* gen) {
  return asynciterator_stop(&gen->iterator, JS_UNDEFINED, gen->ctx);
}

ssize_t
generator_queue(Generator* gen, const void* data, size_t len) {
  ByteBlock blk = block_copy(data, len, gen->ctx);

  return ringbuffer_insert(gen->rbuf, &blk, 1) ? len : 0;
}

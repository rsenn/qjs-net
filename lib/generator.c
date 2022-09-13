#include "generator.h"

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
    buffer_free_rt(&gen->buffer, JS_GetRuntime(gen->ctx));
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
    // gen->iterator.ctx = ctx;
  }
  return gen;
}

JSValue
generator_next(struct generator* gen, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;

  ret = asynciterator_next(&gen->iterator, ctx);

  if(buffer_HEAD(&gen->buffer)) {
    size_t len;
    JSValue value = buffer_toarraybuffer_size(&gen->buffer, &len, ctx);
    gen->buffer = BUFFER_0();

    asynciterator_yield(&gen->iterator, value, ctx);
    gen->bytes_read += len;
  }

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

  return ret;
}

BOOL
generator_close(struct generator* gen, JSContext* ctx) {
  return asynciterator_stop(&gen->iterator, JS_UNDEFINED, ctx);
}

ssize_t
generator_queue(struct generator* gen, const void* data, size_t len) {
  ssize_t ret;

  if((ret = buffer_append(&gen->buffer, data, len, gen->ctx)) > 0)
    gen->bytes_written += len;

  return ret;
}

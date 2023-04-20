#include "generator.h"
#include <assert.h>

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
    gen->callback = JS_NULL;
  }
  return gen;
}

JSValue
generator_dequeue(Generator* gen, BOOL* done_p) {
  ByteBlock blk = queue_next(gen->q, done_p);
  JSValue ret = block_SIZE(&blk) ? gen->block_fn(&blk, gen->ctx) : JS_UNDEFINED;

  if(block_BEGIN(&blk)) {
    gen->bytes_read += block_SIZE(&blk);
    gen->chunks_read += 1;
  }

  return ret;
}

static int
generator_update(Generator* gen) {
  int i = 0;

  while(!list_empty(&gen->iterator.reads) && gen->q && !queue_closed(gen->q)) {
    BOOL done = FALSE;
    JSValue chunk = generator_dequeue(gen, &done);
    // printf("%-22s i: %i reads: %zu q->items: %zu done: %i\n", __func__, i, list_size(&gen->iterator.reads), gen->q ? list_size(&gen->q->items) : 0, done);

    done ? asynciterator_stop(&gen->iterator, gen->ctx) : asynciterator_yield(&gen->iterator, chunk, gen->ctx);
    JS_FreeValue(gen->ctx, chunk);

    if(JS_IsFunction(gen->ctx, gen->callback)) {
      JS_FreeValue(gen->ctx, JS_Call(gen->ctx, gen->callback, JS_UNDEFINED, 0, 0));
      JS_FreeValue(gen->ctx, gen->callback);
      gen->callback = JS_NULL;
    }

    ++i;
  }

  return i;
}

JSValue
generator_next(Generator* gen) {
  JSValue ret = JS_UNDEFINED;

  ret = asynciterator_next(&gen->iterator, gen->ctx);
  uint32_t id = list_empty(&gen->iterator.reads) ? 0 : ((AsyncRead*)gen->iterator.reads.next)->id;
  size_t rds1 = list_size(&gen->iterator.reads);

  size_t upd = generator_update(gen);

  // printf("%-22s gen: %p reads: %zu updated: %zu read: %i\n", __func__, gen, list_size(&gen->iterator.reads), rds1 - list_size(&gen->iterator.reads), id);

  return ret;
}

ssize_t
generator_write(Generator* gen, const void* data, size_t len, JSValueConst callback) {
  ByteBlock blk = block_copy(data, len);
  ssize_t ret = -1, size = block_SIZE(&blk);

  // printf("%-22s gen: %p reads: %zu\n", __func__, gen, list_size(&gen->iterator.reads));

  if(!list_empty(&gen->iterator.reads) && (!gen->q || !gen->q->continuous)) {

    JSValue chunk = gen->block_fn(&blk, gen->ctx);
    if(asynciterator_yield(&gen->iterator, chunk, gen->ctx))
      ret = size;

    JS_FreeValue(gen->ctx, chunk);
  } else {
    ret = enqueue_block(gen, blk, callback);
    // printf("%-22s reads: %zu continuous: %i queued: %zu\n", __func__, list_size(&gen->iterator.reads), gen->q && gen->q->continuous, gen->q ? queue_size(gen->q) : 0);
  }

  if(ret >= 0) {
    gen->bytes_written += ret;
    gen->chunks_written += 1;
  }

  return ret;
}

JSValue
generator_push(Generator* gen, JSValueConst value) {
  ResolveFunctions funcs = {JS_NULL, JS_NULL};
  JSValue ret = js_async_create(gen->ctx, &funcs);

  // printf("%-22s reads: %zu value: %.*s\n", __func__, list_size(&gen->iterator.reads), 10, JS_ToCString(gen->ctx, value));

  if(!generator_yield(gen, value, funcs.resolve)) {
    JS_FreeValue(gen->ctx, JS_Call(gen->ctx, funcs.reject, JS_UNDEFINED, 0, 0));
  }

  js_async_free(gen->ctx, &funcs);
  return ret;
}

BOOL
generator_yield(Generator* gen, JSValueConst value, JSValueConst callback) {
  ssize_t ret;

  if(asynciterator_yield(&gen->iterator, value, gen->ctx)) {
    JSBuffer buf = js_input_chars(gen->ctx, value);
    ret = buf.size;
    js_buffer_free(&buf, gen->ctx);

    if(JS_IsFunction(gen->ctx, callback))
      JS_FreeValue(gen->ctx, JS_Call(gen->ctx, callback, JS_UNDEFINED, 0, 0));

  } else {
    if((ret = enqueue_value(gen, value, callback)) < 0)
      return FALSE;

    if(!gen->q || !queue_continuous(gen->q)) {
      JS_FreeValue(gen->ctx, gen->callback);
      gen->callback = JS_DupValue(gen->ctx, callback);
    }
  }

  if(ret >= 0) {
    gen->bytes_written += ret;
    gen->chunks_written += 1;
  }

  return TRUE;
}

BOOL
generator_close(Generator* gen, JSValueConst callback) {
  BOOL ret = FALSE;
  QueueItem* item = 0;

  if(gen->q) {
    if(!queue_complete(gen->q)) {
      item = queue_close(gen->q);
      ret = TRUE;
    }

    if(gen->q->continuous) {
      if((item = queue_last_chunk(gen->q))) {
        if(item->unref) {
          JSValue chunk = block_SIZE(&item->block) ? gen->block_fn(&item->block, gen->ctx) : JS_UNDEFINED;
          deferred_call(item->unref, chunk);
          JS_FreeValue(gen->ctx, chunk);

          // queue_next(gen->q, NULL);
          gen->closed = TRUE;
          return TRUE;
        }
      }
    }
  }

  gen->closing = TRUE;

  if(asynciterator_stop(&gen->iterator, gen->ctx))
    ret = TRUE;

  return ret;
}

BOOL
generator_continuous(Generator* gen, JSValueConst callback) {
  Queue* q;

  assert(JS_IsNull(gen->callback));

  if(!(q = gen->q))
    q = gen->q = queue_new(gen->ctx);

  if(q) {
    QueueItem* item;

    if((item = queue_continuous(q))) {

      if(JS_IsFunction(gen->ctx, callback)) {
        gen->callback = JS_DupValue(gen->ctx, callback);
        // item->unref = deferred_newjs(JS_DupValue(gen->ctx, callback), gen->ctx);
        // item->unref = deferred_new(&JS_Call, gen->ctx, JS_DupValue(gen->ctx, callback), JS_UNDEFINED);
      }
    }

    return item != NULL;
  }
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
      item->unref = deferred_newjs(JS_DupValue(gen->ctx, callback), gen->ctx);
  }
  return ret;
}

static ssize_t
enqueue_value(Generator* gen, JSValueConst value, JSValueConst callback) {
  JSBuffer buf = js_input_chars(gen->ctx, value);
  ByteBlock blk = block_copy(buf.data, buf.size);
  ssize_t ret;
  js_buffer_free(&buf, gen->ctx);

  if((ret = enqueue_block(gen, blk, callback)) == -1)
    block_free(&blk);

  return ret;
}

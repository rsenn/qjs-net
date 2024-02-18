/**
 * @file generator.c
 */
#include "generator.h"
#include <assert.h>

static JSValue
dequeue_value(Generator* gen, BOOL* done_p, BOOL* binary_p) {
  ByteBlock blk = queue_next(gen->q, done_p, binary_p);
  JSValue ret = block_SIZE(&blk) ? gen->block_fn(&blk, gen->ctx) : JS_UNDEFINED;

  if(block_BEGIN(&blk)) {
    gen->bytes_read += block_SIZE(&blk);
    gen->chunks_read += 1;
  }

  return ret;
}

static Queue*
create_queue(Generator* gen) {
  if(!gen->q) {

#ifdef DEBUG_OUTPUT_
    printf("Creating Queue... %s\n", JS_ToCString(gen->ctx, gen->callback));
#endif
    gen->q = queue_new(gen->ctx);
  }

  return gen->q;
}

static ssize_t
enqueue_block(Generator* gen, ByteBlock blk, JSValueConst callback) {
  QueueItem* item;
  ssize_t ret = block_SIZE(&blk);

#ifdef DEBUG_OUTPUT_
  printf("%s blk.size=%zu\n", __func__, block_SIZE(&blk));
#endif

  create_queue(gen);

  if(gen->buffering)
    gen->q->continuous = TRUE;

  item = queue_put(gen->q, blk, gen->ctx);

  if(gen->buffering)
    gen->q->continuous = FALSE;

  if(item) {

    if(gen->buffering) {
      blk = item->block;
      ret = block_SIZE(&blk);

#ifdef DEBUG_OUTPUT
      printf("%s ret=%zu\n", __func__, ret);
#endif

      if(ret > gen->chunk_size) {
        size_t pos = 0, end;
        item->block = block_slice(&blk, pos, pos + gen->chunk_size);

        pos += gen->chunk_size;

        while(pos < ret) {
          end = MIN(ret, pos + gen->chunk_size);

          queue_add(gen->q, block_slice(&blk, pos, end));
          pos = end;
        }
      }

    } else {
      if(JS_IsFunction(gen->ctx, callback))
        item->unref = deferred_newjs(JS_DupValue(gen->ctx, callback), gen->ctx);
    }
  }

  return ret;
}

static ssize_t
enqueue_value(Generator* gen, JSValueConst value, JSValueConst callback) {
  ssize_t ret;
  JSBuffer buf = js_input_chars(gen->ctx, value);
  ByteBlock blk = block_copy(buf.data, buf.size);

  js_buffer_free(&buf, JS_GetRuntime(gen->ctx));

  if((ret = enqueue_block(gen, blk, callback)) == -1)
    block_free(&blk);

  return ret;
}

static BOOL
start_executor(Generator* gen) {
  if(JS_IsFunction(gen->ctx, gen->executor)) {
    JSValue tmp, cb = gen->executor;
    gen->executor = JS_UNDEFINED;

    tmp = JS_Call(gen->ctx, cb, JS_UNDEFINED, 0, 0);
    JS_FreeValue(gen->ctx, cb);

    if(js_is_promise(gen->ctx, tmp))
      gen->executor = tmp;
    else
      JS_FreeValue(gen->ctx, tmp);

    return TRUE;
  }

  return FALSE;
}

static void
generator_callback(Generator* gen, JSValueConst argument) {
  if(JS_IsFunction(gen->ctx, gen->callback)) {
    JSValue cb = gen->callback;
    gen->callback = JS_NULL;
    JS_FreeValue(gen->ctx, JS_Call(gen->ctx, cb, JS_UNDEFINED, 1, &argument));
    JS_FreeValue(gen->ctx, cb);
  }
}

static int
generator_update(Generator* gen) {
  int i = 0;

  while(!list_empty(&gen->iterator.reads) && gen->q && queue_size(gen->q)) {
    size_t s = block_SIZE(&queue_front(gen->q)->block);

    if(!gen->closing)
      if(gen->buffering && s < gen->chunk_size)
        break;

    BOOL done = FALSE, binary = FALSE;
    JSValue chunk = dequeue_value(gen, &done, &binary);

#ifdef DEBUG_OUTPUT
    printf("%-22s i: %i queue: %zu/%zub dequeued: %zu done: %i\n", __func__, i, gen->q ? queue_size(gen->q) : 0, gen->q ? queue_bytes(gen->q) : 0, s, done);
#endif

    // asynciterator_emplace(&gen->iterator, chunk, done, gen->ctx);
    done ? asynciterator_stop(&gen->iterator, JS_UNDEFINED, gen->ctx) : asynciterator_yield(&gen->iterator, chunk, gen->ctx);

    JS_FreeValue(gen->ctx, chunk);

    if(done)
      gen->closed = TRUE;

    ++i;
  }

#ifdef DEBUG_OUTPUT
  printf("%-22s gen: %p chunk_size: %zu i: %zu reads: %zu continuous: %i buffering: %i closing: %i closed: %i r/w: %zu/%zu queue: %zu/%zub\n",
         __func__,
         (uint32_t)gen,
         gen->chunk_size,
         i,
         list_size(&gen->iterator.reads),
         (gen->q && gen->q->continuous),
         gen->buffering,
         gen->closing,
         gen->closed,
         gen->bytes_read,
         gen->bytes_written,
         gen->q ? queue_size(gen->q) : 0,
         gen->q ? queue_bytes(gen->q) : 0);
#endif
  return i;
}

/**
 * \defgroup generator generator
 *
 * Async generator object
 * @{
 */
static void
generator_zero(Generator* gen) {
  memset(gen, 0, sizeof(Generator));
  asynciterator_zero(&gen->iterator);
  gen->q = 0;
  gen->bytes_written = 0;
  gen->bytes_read = 0;
  gen->chunks_written = 0;
  gen->chunks_read = 0;
  gen->ref_count = 0;
  gen->executor = JS_UNDEFINED;
  gen->resolve_reject = (ResolveFunctions){JS_UNDEFINED, JS_UNDEFINED};
}

/**
 * Creates a new Generator.
 *
 * @param ctx    QuickJS context
 *
 * @return  Pointer to generator struct
 */
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

/**
 * Destroys Generator.
 *
 * @param gen   Pointer to generator struct
 */
void
generator_free(Generator* gen) {
  if(--gen->ref_count == 0) {
    asynciterator_clear(&gen->iterator, JS_GetRuntime(gen->ctx));

    if(gen->q)
      queue_free(gen->q, JS_GetRuntime(gen->ctx));

    js_free(gen->ctx, gen);
  }
}

/**
 * Gets a Promise for the next value the generator is yielding.
 *
 * @param gen   pointer to generator struct
 * @param arg   value to which the Promise returned by the previous push() call
 *              will resolve
 *
 * @return a Promise
 */
JSValue
generator_next(Generator* gen, JSValueConst arg) {
  JSValue ret = JS_UNDEFINED;

#ifdef DEBUG_OUTPUT
  printf("%-22s gen: %p chunk_size: %zu reads: %zu pending: %zu queue: %zu/%zub\n",
         __func__,
         (uint32_t)gen,
         gen->chunk_size,
         list_size(&gen->iterator.reads),
         asynciterator_pending(&gen->iterator),
         gen->q ? queue_size(gen->q) : 0,
         gen->q ? queue_bytes(gen->q) : 0);
#endif

  ret = asynciterator_next(&gen->iterator, arg, gen->ctx);

  if(!start_executor(gen) && !gen->started) {
    generator_callback(gen, arg);
    gen->started = TRUE;
  }

  int n = gen->q ? generator_update(gen) : 0;

  if(n == 0) {
    if(gen->closing || gen->closed) {
      if(asynciterator_stop(&gen->iterator, JS_UNDEFINED, gen->ctx)) {
        gen->closing = FALSE;
        gen->closed = TRUE;
      }
    }
  }

#ifdef DEBUG_OUTPUT
  printf("%-22s gen: %p chunk_size: %zu reads: %zu ret: '%s' buffering: %i closing: %i closed: %i r/w: %zu/%zu queue: %zu/%zub\n",
         __func__,
         (uint32_t)gen,
         gen->chunk_size,
         list_size(&gen->iterator.reads),
         JS_ToCString(gen->ctx, ret),
         gen->buffering,
         gen->closing,
         gen->closed,
         gen->bytes_written,
         gen->bytes_read,
         gen->q ? queue_size(gen->q) : 0,
         gen->q ? queue_bytes(gen->q) : 0);
#endif
  return ret;
}

/**
 * Writes binary data to the generator.
 *
 * @param gen      Pointer to generator struct
 * @param data     Pointer to data
 * @param len      Byte-length of data
 * @param callback Function that will be called as soon as the chunk is dequeued
 *
 * @return bytes written or -1 on error
 */
ssize_t
generator_write(Generator* gen, const void* data, size_t len, JSValueConst callback) {
  ByteBlock blk = block_copy(data, len);
  ssize_t ret = -1, size = block_SIZE(&blk);

  if(!gen->buffering && !(gen->q && gen->q->continuous) && (!gen->q || !queue_size(gen->q)) && asynciterator_pending(&gen->iterator)) {

    JSValue chunk = gen->block_fn(&blk, gen->ctx);

    gen->bytes_read += block_SIZE(&blk);
    gen->chunks_read += 1;

    if(asynciterator_yield(&gen->iterator, chunk, gen->ctx))
      ret = size;

    JS_FreeValue(gen->ctx, chunk);
  } else {
    ret = enqueue_block(gen, blk, callback);

    // gen->buffering = !queue_empty(gen->q);

    while(asynciterator_pending(&gen->iterator) && gen->q && !queue_empty(gen->q) && block_SIZE(&queue_front(gen->q)->block) >= gen->chunk_size) {
      BOOL done = FALSE, binary = FALSE;

#ifdef DEBUG_OUTPUT
      printf("%s block_SIZE(&blk) = %zu\n", __func__, block_SIZE(&blk));
#endif

      JSValue chunk = dequeue_value(gen, &done, &binary);
      asynciterator_emplace(&gen->iterator, chunk, done, gen->ctx);

      JS_FreeValue(gen->ctx, chunk);
    }
  }

  if(ret >= 0) {
    gen->bytes_written += ret;
    gen->chunks_written += 1;
  }

#ifdef DEBUG_OUTPUT
  printf("%-22s gen: %p chunk_size: %zu data: '%.*s' len: %zu chunk_size: %zu reads: %zu continuous: %i buffering: %i closing: %i closed: %i r/w: %zu/%zu queue: %zu/%zub\n",
         __func__,
         (uint32_t)gen,
         gen->chunk_size,
         MIN(10, (int)len),
         data,
         len,
         gen->chunk_size,
         list_size(&gen->iterator.reads),
         (gen->q && gen->q->continuous),
         gen->buffering,
         gen->closing,
         gen->closed,
         gen->bytes_read,
         gen->bytes_written,
         gen->q ? queue_size(gen->q) : 0,
         gen->q ? queue_bytes(gen->q) : 0);
#endif
  return ret;
}

/**
 * Pushes a JSValue to the generator
 *
 * @param gen      Pointer to generator struct
 * @param value    Value to push
 *
 * @return Promise that is resolved on the subsequent call to generator_next()
 */
JSValue
generator_push(Generator* gen, JSValueConst value) {
  JSValue ret;

  ret = js_async_create(gen->ctx, &gen->resolve_reject);

  if(!generator_yield(gen, value, JS_UNDEFINED))
    js_async_reject(gen->ctx, &gen->resolve_reject, JS_UNDEFINED);

#ifdef DEBUG_OUTPUT
  printf("%-22s gen: %p chunk_size: %zu reads: %zu value: '%s' buffering: %i closing: %i closed: %i r/w: %zu/%zu queue: %zu/%zub\n",
         __func__,
         (uint32_t)gen,
         gen->chunk_size,
         list_size(&gen->iterator.reads),
         JS_ToCString(gen->ctx, value),
         gen->buffering,
         gen->closing,
         gen->closed,
         gen->bytes_read,
         gen->bytes_written,
         gen->q ? queue_size(gen->q) : 0,
         gen->q ? queue_bytes(gen->q) : 0);
#endif

  gen->promise = js_promise_wrap(gen->ctx, ret);
  JS_FreeValue(gen->ctx, ret);
  return JS_DupValue(gen->ctx, gen->promise);
}

/**
 * Causes the previous push call to reject with the given error.
 * If the executor fails to handle the error, the throw method rethrows the error
 * and finishs the generator. A throw method call is detected as unhandled in the
 * following situations:
 *
 * - The generator has not started (Repeater.prototype.next has never been called).
 * - The generator has stopped.
 * - The generator has a non-empty queue.
 * - The promise returned from the previous push call has not been awaited and its
 *   then/catch methods have not been called.
 *
 * @param gen    The generator
 * @param error  The error to send to the generator.
 *
 * @return  A promise which fulfills to the next iteration result if the
 *          generator handles the error, and otherwise rejects with the given error.
 */
JSValue
generator_throw(Generator* gen, JSValueConst error) {
  JSValue ret = JS_UNDEFINED;
  JSWrappedPromiseRecord* wpr = js_wrappedpromise_data(gen->promise);

  BOOL unhandled = !generator_started(gen) || generator_stopped(gen) || (gen->q && !queue_empty(gen->q)) || (wpr && !(wpr->thened || wpr->catched));

  if(unhandled) {
    ResolveFunctions funcs;
    ret = js_async_create(gen->ctx, &funcs);
    js_async_reject(gen->ctx, &funcs, error);

    generator_stop(gen, JS_UNDEFINED);
  } else {
    js_async_reject(gen->ctx, &gen->resolve_reject, error);
    ret = generator_next(gen, JS_UNDEFINED);
  }

  return ret;
}

/**
 * Yields a value.
 *
 * @param gen      Pointer to generator struct
 * @param value    Value to yield
 *
 * @return Promise that is resolved on the subsequent call to generator_next()
 */
BOOL
generator_yield(Generator* gen, JSValueConst value, JSValueConst callback) {
  ssize_t ret;

  if(gen->closing || gen->closed)
    return FALSE;

  if(asynciterator_yield(&gen->iterator, value, gen->ctx)) {
    JSBuffer buf = js_input_chars(gen->ctx, value);
    ret = buf.size;
    js_buffer_free(&buf, JS_GetRuntime(gen->ctx));
  } else {
    if((ret = enqueue_value(gen, value, callback)) < 0)
      return FALSE;
  }

  if(ret >= 0) {
    gen->bytes_written += ret;
    gen->chunks_written += 1;
  }

  return ret >= 0;
}

/**
 * Stops a generator
 *
 * @param gen      Pointer to generator struct
 * @param arg      Value of the last iteration result (done: true)
 *
 * @return TRUE when successful, FALSE otherwise
 */
BOOL
generator_stop(Generator* gen, JSValueConst arg) {
  BOOL ret = FALSE;
  Queue* q;
  QueueItem* item = 0;

#ifdef DEBUG_OUTPUT
  printf("%-22s gen: %p chunk_size: %zu arg: '%s' closed: %zu queue: %zu/%zub\n",
         __func__,
         (uint32_t)gen,
         gen->chunk_size,
         JS_ToCString(gen->ctx, arg),
         gen->closed,
         gen->q ? queue_size(gen->q) : 0,
         gen->q ? queue_bytes(gen->q) : 0);
#endif

  if(gen->closed)
    return ret;

  if((q = gen->q)) {
    if(!queue_complete(q)) {
      item = queue_close(q);
      ret = TRUE;
    }

    if(q->continuous) {
      if((item = queue_last_chunk(q))) {
        JSValue chunk = block_SIZE(&item->block) ? gen->block_fn(&item->block, gen->ctx) : JS_UNDEFINED;

        if(item->unref)
          deferred_call(item->unref, chunk);

        if(JS_IsFunction(gen->ctx, gen->callback))
          JS_Call(gen->ctx, gen->callback, JS_NULL, 1, &chunk);

        JS_FreeValue(gen->ctx, chunk);
      }
    }
  } else {
    if(asynciterator_pending(&gen->iterator))
      ret = asynciterator_stop(&gen->iterator, arg, gen->ctx);
    else {
      gen->closing = TRUE;
      ret = TRUE;
    }
  }

  return ret;
}

/**
 * Puts a generator in continuous mode.
 * A queue will be created and all data will be accumulated and only one final
 * value will be yielded.
 *
 * @param gen      Pointer to generator struct
 * @param callback Function which is called when generator is complete
 *
 * @return TRUE when successful, FALSE otherwise
 */
BOOL
generator_continuous(Generator* gen, JSValueConst callback) {
  Queue* q;
  // printf("generator_continuous(%s)\n", JS_ToCString(gen->ctx, callback));

  if((q = create_queue(gen))) {
    QueueItem* item;

    if((item = queue_continuous(q))) {

      if(JS_IsFunction(gen->ctx, callback)) {
        gen->callback = JS_DupValue(gen->ctx, callback);
        /*     item->unref = deferred_newjs(JS_DupValue(gen->ctx, callback), gen->ctx);
             item->unref = deferred_new(&JS_Call, gen->ctx, JS_DupValue(gen->ctx, callback), JS_UNDEFINED);*/
      }

      q->continuous = TRUE;
    }

    return item != NULL;
  }

  return q != NULL;
}

/**
 * Finishes a generator in continuous mode.
 * The callback given to generator_continuous() will be called with all
 * accumulated values.
 *
 * @param gen      Pointer to generator struct
 *
 * @return TRUE when successful, FALSE otherwise
 */
BOOL
generator_finish(Generator* gen) {

  gen->closing = TRUE;

  if((gen->q && gen->q->continuous) && !JS_IsNull(gen->callback)) {
    BOOL done = FALSE, binary = FALSE;
    JSValue ret = dequeue_value(gen, &done, &binary);

    JS_Call(gen->ctx, gen->callback, JS_UNDEFINED, 1, &ret);
    JS_FreeValue(gen->ctx, ret);
    return TRUE;
  }

  generator_update(gen);

  return FALSE;
}

/**
 * Enqueues a value, without yielding.
 * If not already existing, a queue is created.
 *
 * @param gen      Pointer to generator struct
 * @param value    Value to enqueue
 *
 * @return Number of enqueued bytes or -1 on error
 */
ssize_t
generator_enqueue(Generator* gen, JSValueConst value) {
  create_queue(gen);
  return enqueue_value(gen, value, JS_UNDEFINED);
}

BOOL
generator_buffering(Generator* gen, size_t chunk_size) {
  Queue* q;
  // printf("generator_continuous(%s)\n", JS_ToCString(gen->ctx, callback));

  if((q = create_queue(gen))) {
    QueueItem* item;

    gen->chunk_size = chunk_size;
    gen->buffering = TRUE;
  }

  return q != NULL;
}

/**
 * @}
 */

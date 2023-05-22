/**
 * @file generator.h
 */
#ifndef QJSNET_LIB_GENERATOR_H
#define QJSNET_LIB_GENERATOR_H

#include "buffer.h"
#include "asynciterator.h"
#include "queue.h"

typedef struct generator {
  union {
    AsyncIterator iterator;
    struct {
      int ref_count;
      struct list_head reads;
      BOOL closed, closing;
    };
  };
  JSContext* ctx;
  Queue* q;
  JSValue executor;
  union {
    JSValue callback;
    ResolveFunctions promise;
  };
  uint64_t bytes_written, bytes_read;
  uint32_t chunks_written, chunks_read;
  JSValue (*block_fn)(ByteBlock*, JSContext*);
} Generator;

void generator_free(Generator*);
Generator* generator_new(JSContext*);
JSValue generator_dequeue(Generator*, BOOL* done_p);
JSValue generator_next(Generator*, JSValueConst arg);
ssize_t generator_write(Generator*, const void* data, size_t len, JSValueConst callback);
JSValue generator_push(Generator*, JSValueConst value);

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
 * @returns  A promise which fulfills to the next iteration result if the
 *           generator handles the error, and otherwise rejects with the given error.
 */
JSValue generator_throw(Generator* gen, JSValueConst error);

BOOL generator_yield(Generator*, JSValueConst value, JSValueConst callback);
BOOL generator_stop(Generator*, JSValueConst callback);
BOOL generator_continuous(Generator*, JSValueConst callback);
Queue* generator_queue(Generator*);
BOOL generator_finish(Generator* gen);
ssize_t generator_enqueue(Generator* gen, JSValueConst value);

static inline Generator*
generator_dup(Generator* gen) {
  ++gen->ref_count;
  return gen;
}

#endif /* QJSNET_LIB_GENERATOR_H */

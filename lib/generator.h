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
  JSValue executor, promise;
  union {
    JSValue callback;
    ResolveFunctions resolve_reject;
  };
  uint64_t bytes_written, bytes_read;
  uint32_t chunks_written, chunks_read;
  uint32_t chunk_size;
  BOOL started, buffering;
  JSValue (*block_fn)(ByteBlock*, JSContext*);
} Generator;

void generator_free(Generator*);
Generator* generator_new(JSContext*);
JSValue generator_next(Generator*, JSValueConst arg);
ssize_t generator_write(Generator*, const void* data, size_t len, JSValueConst callback);
JSValue generator_push(Generator*, JSValueConst value);
JSValue generator_throw(Generator* gen, JSValueConst error);
BOOL generator_yield(Generator*, JSValueConst value, JSValueConst callback);
BOOL generator_stop(Generator*, JSValueConst);
BOOL generator_continuous(Generator*, JSValueConst callback);
BOOL generator_buffering(Generator*, size_t chunk_size);
BOOL generator_finish(Generator* gen);
ssize_t generator_enqueue(Generator* gen, JSValueConst value);

static inline Generator*
generator_dup(Generator* gen) {
  ++gen->ref_count;
  return gen;
}

static inline BOOL
generator_started(Generator* gen) {
  return !JS_IsFunction(gen->ctx, gen->executor);
}

static inline BOOL
generator_stopped(Generator* gen) {
  return gen->closing || gen->closed;
}

static inline uint32_t
generator_written(Generator* gen) {
  return gen->bytes_written;
}
static inline uint32_t
generator_read(Generator* gen) {
  return gen->bytes_read;
}

#endif /* QJSNET_LIB_GENERATOR_H */

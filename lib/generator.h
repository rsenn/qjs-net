#ifndef QJSNET_LIB_GENERATOR_H
#define QJSNET_LIB_GENERATOR_H

#include "buffer.h"
#include "asynciterator.h"
#include "queue.h"

typedef struct generator {
  union {
    AsyncIterator iterator;
    struct {
      struct list_head reads;
      BOOL closed, closing;
    };
  };
  JSContext* ctx;
  Queue* q;
  JSValue callback;
  uint64_t bytes_written, bytes_read;
  uint32_t chunks_written, chunks_read;
  JSValue (*block_fn)(ByteBlock*, JSContext*);
  int ref_count;
} Generator;

void       generator_zero(Generator*);
void       generator_destroy(Generator**);
BOOL       generator_free(Generator*);
Generator* generator_new(JSContext*);
JSValue    generator_dequeue(Generator*, BOOL* done_p);
JSValue    generator_next(Generator*);
ssize_t    generator_write(Generator*, const void* data, size_t len, JSValueConst callback);
JSValue    generator_push(Generator*, JSValueConst value);
BOOL       generator_yield(Generator*, JSValueConst value, JSValueConst callback);
BOOL       generator_close(Generator*, JSValueConst callback);
BOOL       generator_continuous(Generator*, JSValueConst callback);


static inline Generator*
generator_dup(Generator* gen) {
  ++gen->ref_count;
  return gen;
}

#endif /* QJSNET_LIB_GENERATOR_H */

#ifndef QJSNET_LIB_GENERATOR_H
#define QJSNET_LIB_GENERATOR_H

#include "buffer.h"
#include "asynciterator.h"
#include "ringbuffer.h"

typedef struct generator {
  union {
    AsyncIterator iterator;
    struct {
      JSContext* ctx;
      BOOL closed, closing;
    };
  };
  struct ringbuffer* rbuf;
  uint64_t bytes_written, bytes_read;
  uint64_t chunks_written, chunks_read;
  int ref_count;
} Generator;

void generator_zero(Generator*);
void generator_destroy(Generator**);
BOOL generator_free(Generator*);
Generator* generator_new(JSContext*);
JSValue generator_next(Generator*, JSContext*);
ssize_t generator_queue(Generator*, const void*, size_t);
ssize_t generator_write(Generator*, const void*, size_t);
BOOL generator_close(Generator*);

static inline Generator*
generator_dup(Generator* gen) {
  ++gen->ref_count;
  return gen;
}

#endif /* QJSNET_LIB_GENERATOR_H */

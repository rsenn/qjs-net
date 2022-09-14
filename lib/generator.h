#ifndef QUICKJS_NET_LIB_GENERATOR_H
#define QUICKJS_NET_LIB_GENERATOR_H

#include "buffer.h"
#include "asynciterator.h" // for AsyncIterator

struct generator {
  ByteBuffer buffer;
  union {
    AsyncIterator iterator;
    struct {
      JSContext* ctx;
      BOOL closed, closing;
    };
  };
  uint64_t bytes_written, bytes_read;
  int ref_count;
};

void generator_zero(struct generator*);
void generator_destroy(struct generator**);
BOOL generator_free(struct generator*);
struct generator* generator_new(JSContext*);
JSValue generator_next(struct generator*, JSContext*);
ssize_t generator_queue(struct generator*, const void*, size_t);
ssize_t generator_write(struct generator*, const void*, size_t);
BOOL generator_close(struct generator*, JSContext* ctx);
JSValue minnet_generator_create(JSContext*, struct generator**);

static inline struct generator*
generator_dup(struct generator* gen) {
  ++gen->ref_count;
  return gen;
}
#endif /* QUICKJS_NET_LIB_GENERATOR_H */

#ifndef QUICKJS_NET_LIB_GENERATOR_H
#define QUICKJS_NET_LIB_GENERATOR_H

#include "jsutils.h"
#include <libwebsockets.h>
#include <pthread.h>
#include "buffer.h"

typedef struct generator {
  MinnetBuffer buffer;
  union {
    AsyncIterator iterator;
    struct {
      JSContext* ctx;
      BOOL closed, closing;
    };
  };
  uint64_t bytes_written, bytes_read;
  int ref_count;
} MinnetGenerator;

void generator_zero(struct generator*);
void generator_destroy(struct generator**);
BOOL generator_free(struct generator*);
struct generator* generator_new(JSContext*);
JSValue generator_next(MinnetGenerator*, JSContext*);
ssize_t generator_queue(MinnetGenerator*, const void*, size_t);
ssize_t generator_write(MinnetGenerator*, const void*, size_t);
BOOL generator_close(MinnetGenerator*, JSContext* ctx);
JSValue minnet_generator_create(JSContext*, MinnetGenerator**);

static inline struct generator*
generator_dup(struct generator* gen) {
  ++gen->ref_count;
  return gen;
}
#endif /* QUICKJS_NET_LIB_GENERATOR_H */

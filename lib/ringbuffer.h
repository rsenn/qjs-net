#ifndef QJSNET_LIB_RINGBUFFER_H
#define QJSNET_LIB_RINGBUFFER_H

#include <quickjs.h>
#include <cutils.h>
#include "buffer.h"
#include <libwebsockets.h>
#include <pthread.h>

struct ringbuffer {
  size_t ref_count;
  char type[256];
  struct lws_ring* ring;
  pthread_mutex_t lock_ring; /* serialize access to the ring buffer */
};

void ringbuffer_dump(struct ringbuffer const*);
void ringbuffer_destroy_element(void*);
void ringbuffer_init(struct ringbuffer*, size_t element_len, size_t count, const char* type, size_t typelen);
struct ringbuffer* ringbuffer_new(JSContext*);
void ringbuffer_init2(struct ringbuffer*, size_t element_len, size_t count);
struct ringbuffer* ringbuffer_new2(size_t, size_t count, JSContext* ctx);
size_t ringbuffer_insert(struct ringbuffer*, const void* ptr, size_t n);
size_t ringbuffer_consume(struct ringbuffer*, void* ptr, size_t n);
size_t ringbuffer_skip(struct ringbuffer*, size_t n);
const void* ringbuffer_next(struct ringbuffer*);
size_t ringbuffer_size(struct ringbuffer*);
size_t ringbuffer_avail(struct ringbuffer*);
void ringbuffer_zero(struct ringbuffer*);
void ringbuffer_free(struct ringbuffer*, JSRuntime* rt);

static inline int
ringbuffer_lock(struct ringbuffer* strm) {
  return pthread_mutex_lock(&strm->lock_ring);
}

static inline int
ringbuffer_unlock(struct ringbuffer* strm) {
  return pthread_mutex_unlock(&strm->lock_ring);
}
#endif /* QJSNET_LIB_RINGBUFFER_H */

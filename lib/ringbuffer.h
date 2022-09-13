#ifndef QUICKJS_NET_LIB_RINGBUFFER_H
#define QUICKJS_NET_LIB_RINGBUFFER_H

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
void ringbuffer_init(struct ringbuffer*, size_t, size_t, const char* type, size_t typelen);
struct ringbuffer* ringbuffer_new(JSContext*);
void ringbuffer_init2(struct ringbuffer*, size_t, size_t);
struct ringbuffer* ringbuffer_new2(size_t, size_t, JSContext*);
size_t ringbuffer_insert(struct ringbuffer*, const void*, size_t);
size_t ringbuffer_consume(struct ringbuffer*, void*, size_t);
size_t ringbuffer_skip(struct ringbuffer*, size_t);
const void* ringbuffer_next(struct ringbuffer*);
size_t ringbuffer_size(struct ringbuffer*);
size_t ringbuffer_avail(struct ringbuffer*);
void ringbuffer_zero(struct ringbuffer*);
void ringbuffer_free(struct ringbuffer*, JSRuntime*);

static inline int
ringbuffer_lock(struct ringbuffer* strm) {
  return pthread_mutex_lock(&strm->lock_ring);
}

static inline int
ringbuffer_unlock(struct ringbuffer* strm) {
  return pthread_mutex_unlock(&strm->lock_ring);
}
#endif /* QUICKJS_NET_LIB_RINGBUFFER_H */

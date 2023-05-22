/**
 * @file ringbuffer.h
 */
#ifndef QJSNET_LIB_RINGBUFFER_H
#define QJSNET_LIB_RINGBUFFER_H

#include <quickjs.h>
#include "buffer.h"
#include <libwebsockets.h>
#include <pthread.h>

struct ringbuffer {
  int ref_count;
  size_t size, element_len;
  char type[256];
  struct lws_ring* ring;
  pthread_mutex_t lock_ring; /* serialize access to the ring buffer */
};

void ringbuffer_dump(struct ringbuffer const*);
void ringbuffer_destroy_element(void*);
void ringbuffer_init(struct ringbuffer*, size_t element_len, size_t count, const char* type, size_t typelen);
struct ringbuffer* ringbuffer_new(JSContext*);
void ringbuffer_init2(struct ringbuffer*, size_t element_len, size_t count);
size_t ringbuffer_insert(struct ringbuffer*, const void* ptr, size_t n);
size_t ringbuffer_consume(struct ringbuffer*, void* ptr, size_t n);
size_t ringbuffer_skip(struct ringbuffer*, size_t n);
const void* ringbuffer_next(struct ringbuffer*);
size_t ringbuffer_bytelength(struct ringbuffer*);
size_t ringbuffer_waiting(struct ringbuffer*);
size_t ringbuffer_avail(struct ringbuffer*);
void ringbuffer_zero(struct ringbuffer*);
void ringbuffer_free(struct ringbuffer*, JSContext* ctx);
void ringbuffer_free_rt(struct ringbuffer*, JSRuntime* rt);

static inline int
ringbuffer_lock(struct ringbuffer* strm) {
  return pthread_mutex_lock(&strm->lock_ring);
}

static inline struct ringbuffer*
ringbuffer_dup(struct ringbuffer* rb) {
  ++rb->ref_count;
  return rb;
}

static inline int
ringbuffer_unlock(struct ringbuffer* strm) {
  return pthread_mutex_unlock(&strm->lock_ring);
}

static inline size_t
ringbuffer_element_len(struct ringbuffer* rb) {
  return rb->element_len;
}

#endif /* QJSNET_LIB_RINGBUFFER_H */

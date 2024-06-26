/**
 * @file ringbuffer.c
 */
#include "ringbuffer.h"
#include "utils.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>
#include <pthread.h>

void
ringbuffer_destroy_element(void* element) {}

void
ringbuffer_init(struct ringbuffer* rb, size_t element_len, size_t count, const char* type, size_t typelen) {
  if(type)
    pstrcpy(rb->type, MIN(typelen + 1, sizeof(rb->type)), type);

  rb->size = count;
  rb->element_len = element_len;
  rb->ring = lws_ring_create(element_len, count, ringbuffer_destroy_element);

  pthread_mutex_init(&rb->lock_ring, 0);
}

struct ringbuffer*
ringbuffer_new(JSContext* ctx) {
  struct ringbuffer* rb;

  if((rb = js_mallocz(ctx, sizeof(struct ringbuffer))))
    rb->ref_count = 1;

  return rb;
}

size_t
ringbuffer_insert(struct ringbuffer* rb, const void* ptr, size_t n) {
  size_t ret;
  assert(rb->ring);

  pthread_mutex_lock(&rb->lock_ring);

  ret = lws_ring_insert(rb->ring, ptr, n);
  pthread_mutex_unlock(&rb->lock_ring);

  return ret;
}

size_t
ringbuffer_consume(struct ringbuffer* rb, void* ptr, size_t n) {
  size_t ret;
  assert(rb->ring);
  pthread_mutex_lock(&rb->lock_ring);

  ret = lws_ring_consume(rb->ring, 0, ptr, n);

  pthread_mutex_unlock(&rb->lock_ring);
  return ret;
}

size_t
ringbuffer_skip(struct ringbuffer* rb, size_t n) {
  size_t ret;
  assert(rb->ring);
  pthread_mutex_lock(&rb->lock_ring);

  ret = lws_ring_consume(rb->ring, 0, 0, n);

  pthread_mutex_unlock(&rb->lock_ring);
  return ret;
}

const void*
ringbuffer_next(struct ringbuffer* rb) {
  assert(rb->ring);
  return lws_ring_get_element(rb->ring, 0);
}

size_t
ringbuffer_waiting(struct ringbuffer* rb) {
  assert(rb->ring);
  return lws_ring_get_count_waiting_elements(rb->ring, 0);
}

size_t
ringbuffer_bytelength(struct ringbuffer* rb) {
  return rb->size * rb->element_len;
}

size_t
ringbuffer_avail(struct ringbuffer* rb) {
  assert(rb->ring);
  return lws_ring_get_count_free_elements(rb->ring);
}

void
ringbuffer_zero(struct ringbuffer* rb) {
  lws_ring_destroy(rb->ring);
  memset(rb, 0, sizeof(struct ringbuffer));
}

void
ringbuffer_free(struct ringbuffer* rb, JSRuntime* rt) {
  if(--rb->ref_count == 0) {
    ringbuffer_zero(rb);
    js_free_rt(rt, rb);
  }
}

#include "ringbuffer.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>
#include <pthread.h>

void
ringbuffer_dump(struct ringbuffer const* strm) {
  /*  printf("\nstruct ringbuffer {\n\tref_count = %zu", strm->ref_count);
    buffer_dump("buffer", &strm->buffer);
    fputs("\n}", stderr);
    fflush(stderr);*/
}

static void
ringbuffer_destroy_element(void* element) {}

void
ringbuffer_init(struct ringbuffer* strm, size_t element_len, size_t count, const char* type, size_t typelen) {
  //  memset(strm, 0, sizeof(*strm));

  if(type)
    pstrcpy(strm->type, MIN(typelen + 1, sizeof(strm->type)), type);

  strm->ring = lws_ring_create(element_len, count, ringbuffer_destroy_element);

  pthread_mutex_init(&strm->lock_ring, 0);
}

struct ringbuffer*
ringbuffer_new(JSContext* ctx) {
  struct ringbuffer* strm;

  if((strm = js_mallocz(ctx, sizeof(struct ringbuffer))))
    strm->ref_count = 1;

  return strm;
}

void
ringbuffer_init2(struct ringbuffer* strm, size_t element_len, size_t count) {

  const char* type = "application/binary";
  ringbuffer_init(strm, element_len, count, type, strlen(type));
}

struct ringbuffer*
ringbuffer_new2(size_t element_len, size_t count, JSContext* ctx) {
  struct ringbuffer* strm;

  if((strm = ringbuffer_new(ctx)))

    ringbuffer_init2(strm, element_len, count);

  return strm;
}

size_t
ringbuffer_insert(struct ringbuffer* strm, const void* ptr, size_t n) {
  size_t ret;
  assert(strm->ring);

  pthread_mutex_lock(&strm->lock_ring);

  ret = lws_ring_insert(strm->ring, ptr, n);
  pthread_mutex_unlock(&strm->lock_ring);

  return ret;
}

size_t
ringbuffer_consume(struct ringbuffer* strm, void* ptr, size_t n) {
  size_t ret;
  assert(strm->ring);
  pthread_mutex_lock(&strm->lock_ring);

  ret = lws_ring_consume(strm->ring, 0, ptr, n);

  pthread_mutex_unlock(&strm->lock_ring);
  return ret;
}

size_t
ringbuffer_skip(struct ringbuffer* strm, size_t n) {
  size_t ret;
  assert(strm->ring);
  pthread_mutex_lock(&strm->lock_ring);

  ret = lws_ring_consume(strm->ring, 0, 0, n);

  pthread_mutex_unlock(&strm->lock_ring);
  return ret;
}

const void*
ringbuffer_next(struct ringbuffer* strm) {
  assert(strm->ring);
  return lws_ring_get_element(strm->ring, 0);
}

size_t
ringbuffer_size(struct ringbuffer* strm) {
  assert(strm->ring);
  return lws_ring_get_count_waiting_elements(strm->ring, 0);
}

size_t
ringbuffer_avail(struct ringbuffer* strm) {
  assert(strm->ring);
  return lws_ring_get_count_free_elements(strm->ring);
}

void
ringbuffer_zero(struct ringbuffer* strm) {
  lws_ring_destroy(strm->ring);
  memset(strm, 0, sizeof(struct ringbuffer));
}

void
ringbuffer_free(struct ringbuffer* strm, JSRuntime* rt) {
  ringbuffer_zero(strm);
  js_free_rt(rt, strm);
}

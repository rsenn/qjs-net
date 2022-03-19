#ifndef MINNET_RINGBUFFER_H
#define MINNET_RINGBUFFER_H

#include <quickjs.h>
#include <cutils.h>
#include "minnet.h"
#include <libwebsockets.h>
#include <pthread.h>

typedef struct ringbuffer {
  size_t ref_count;
  char type[256];

  struct lws_ring* ring;
  pthread_mutex_t lock_ring; /* serialize access to the ring buffer */
} MinnetRingBuffer;

void ringbuffer_dump(struct ringbuffer const*);
void ringbuffer_init(struct ringbuffer*, size_t, size_t, const char* type, size_t typelen);
struct ringbuffer* ringbuffer_new(JSContext*);
struct ringbuffer* ringbuffer_new2(size_t, size_t, JSContext*);
size_t ringbuffer_insert(struct ringbuffer*, const void*, size_t);
size_t ringbuffer_consume(struct ringbuffer*, void*, size_t);
size_t ringbuffer_skip(struct ringbuffer*, size_t);
const void* ringbuffer_next(struct ringbuffer*);
size_t ringbuffer_size(struct ringbuffer*);
size_t ringbuffer_avail(struct ringbuffer*);
void ringbuffer_zero(struct ringbuffer*);
void ringbuffer_free(struct ringbuffer*, JSRuntime*);
JSValue minnet_ringbuffer_constructor(JSContext*, JSValue, int, JSValue argv[]);
JSValue minnet_ringbuffer_new(JSContext*, const char*, size_t, const void* x, size_t n);
JSValue minnet_ringbuffer_wrap(JSContext*, struct ringbuffer*);

extern JSClassDef minnet_ringbuffer_class;
extern THREAD_LOCAL JSValue minnet_ringbuffer_proto, minnet_ringbuffer_ctor;
extern THREAD_LOCAL JSClassID minnet_ringbuffer_class_id;
extern const JSCFunctionListEntry minnet_ringbuffer_proto_funcs[];
extern const size_t minnet_ringbuffer_proto_funcs_size;

static inline int
ringbuffer_lock(struct ringbuffer* strm) {
  return pthread_mutex_lock(&strm->lock_ring);
}
static inline int
ringbuffer_unlock(struct ringbuffer* strm) {
  return pthread_mutex_unlock(&strm->lock_ring);
}

static inline MinnetRingBuffer*
minnet_ringbuffer_data(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_ringbuffer_class_id);
}

#endif /* MINNET_RINGBUFFER_H */

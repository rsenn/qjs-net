#ifndef MINNET_STREAM_H
#define MINNET_STREAM_H

#include <quickjs.h>
#include <cutils.h>
#include "minnet.h"
#include <libwebsockets.h>

typedef struct stream {
  size_t ref_count;
  char type[256];

  struct lws_ring* ring;
  pthread_mutex_t lock_ring; /* serialize access to the ring buffer */
} MinnetStream;

void stream_dump(struct stream const*);
void stream_init(struct stream*, size_t, size_t, const char* type, size_t typelen);
struct stream* stream_new(JSContext*);
struct stream* stream_new2(size_t, size_t, JSContext*);
size_t stream_insert(struct stream*, const void*, size_t);
size_t stream_consume(struct stream*, void*, size_t);
const void* stream_next(struct stream*);
void stream_zero(struct stream*);
void stream_free(struct stream*, JSRuntime*);
JSValue minnet_stream_constructor(JSContext*, JSValue, int, JSValue argv[]);
JSValue minnet_stream_new(JSContext*, const char*, size_t, const void* x, size_t n);
JSValue minnet_stream_wrap(JSContext*, struct stream*);

extern JSClassDef minnet_stream_class;
extern THREAD_LOCAL JSValue minnet_stream_proto, minnet_stream_ctor;
extern THREAD_LOCAL JSClassID minnet_stream_class_id;
extern const JSCFunctionListEntry minnet_stream_proto_funcs[];
extern const size_t minnet_stream_proto_funcs_size;

static inline int
stream_lock(struct stream* strm) {
  return pthread_mutex_lock(&strm->lock_ring);
}
static inline int
stream_unlock(struct stream* strm) {
  return pthread_mutex_unlock(&strm->lock_ring);
}

static inline MinnetStream*
minnet_stream_data(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_stream_class_id);
}

#endif /* MINNET_STREAM_H */

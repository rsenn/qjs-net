#ifndef MINNET_RINGBUFFER_H
#define MINNET_RINGBUFFER_H

#include "ringbuffer.h"

typedef struct ringbuffer MinnetRingbuffer;

JSValue minnet_ringbuffer_constructor(JSContext*, JSValueConst, int, JSValueConst argv[]);
JSValue minnet_ringbuffer_new(JSContext*, const char*, size_t, const void* x, size_t n);
JSValue minnet_ringbuffer_wrap(JSContext*, struct ringbuffer*);

extern JSClassDef minnet_ringbuffer_class;
extern THREAD_LOCAL JSValue minnet_ringbuffer_proto, minnet_ringbuffer_ctor;
extern THREAD_LOCAL JSClassID minnet_ringbuffer_class_id;
extern const JSCFunctionListEntry minnet_ringbuffer_proto_funcs[];
extern const size_t minnet_ringbuffer_proto_funcs_size;

static inline MinnetRingbuffer*
minnet_ringbuffer_data(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_ringbuffer_class_id);
}
#endif /* MINNET_RINGBUFFER_H */

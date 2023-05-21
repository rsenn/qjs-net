#ifndef MINNET_RINGBUFFER_H
#define MINNET_RINGBUFFER_H

#include "utils.h"
#include "ringbuffer.h"

typedef struct ringbuffer MinnetRingbuffer;

JSValue minnet_ringbuffer_constructor(JSContext*, JSValueConst, int, JSValueConst[]);
int minnet_ringbuffer_init(JSContext*, JSModuleDef*);

extern THREAD_LOCAL JSValue minnet_ringbuffer_proto, minnet_ringbuffer_ctor;
extern THREAD_LOCAL JSClassID minnet_ringbuffer_class_id;

static inline MinnetRingbuffer*
minnet_ringbuffer_data(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_ringbuffer_class_id);
}
#endif /* MINNET_RINGBUFFER_H */

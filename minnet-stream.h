#ifndef MINNET_STREAM_H
#define MINNET_STREAM_H

#include <quickjs.h>
#include <cutils.h>
#include "buffer.h"

typedef struct stream {
  struct byte_buffer buffer;
} MinnetStream;

void stream_dump(struct stream const*);
void stream_init(struct stream*, const void* x, size_t len);
struct stream* stream_new(JSContext*);
void stream_zero(struct stream*);
JSValue minnet_stream_constructor(JSContext*, JSValue new_target, int argc, JSValue argv[]);
JSValue minnet_stream_new(JSContext*, const void* x, size_t len);
JSValue minnet_stream_wrap(JSContext*, struct stream*);

extern JSClassDef minnet_stream_class;
extern JSValue minnet_stream_proto, minnet_stream_ctor;
extern JSClassID minnet_stream_class_id;
extern const JSCFunctionListEntry minnet_stream_proto_funcs[];
extern const size_t minnet_stream_proto_funcs_size;

static inline MinnetStream*
minnet_stream_data(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_stream_class_id);
}

#endif /* MINNET_STREAM_H */

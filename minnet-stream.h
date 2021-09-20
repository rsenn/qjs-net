#ifndef MINNET_STREAM_H
#define MINNET_STREAM_H

#include <quickjs.h>
#include <cutils.h>
#include "minnet.h"
#include "buffer.h"

typedef struct stream {
  size_t ref_count;
  char type[256];
  struct byte_buffer buffer;
} MinnetStream;

void stream_dump(struct stream const*);
void stream_init(struct stream*, const char* type, size_t typelen, const void* x, size_t len);
struct stream* stream_new(JSContext*);
void stream_zero(struct stream*);
JSValue minnet_stream_constructor(JSContext*, JSValue new_target, int argc, JSValue argv[]);
JSValue minnet_stream_new(JSContext*, const char* type, size_t typelen, const void* x, size_t len);
JSValue minnet_stream_wrap(JSContext*, struct stream*);

extern JSClassDef minnet_stream_class;
extern THREAD_LOCAL JSValue minnet_stream_proto, minnet_stream_ctor;
extern THREAD_LOCAL JSClassID minnet_stream_class_id;
extern const JSCFunctionListEntry minnet_stream_proto_funcs[];
extern const size_t minnet_stream_proto_funcs_size;

static inline MinnetStream*
minnet_stream_data(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_stream_class_id);
}

#endif /* MINNET_STREAM_H */

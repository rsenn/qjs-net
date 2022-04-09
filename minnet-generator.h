#ifndef MINNET_GENERATOR_H
#define MINNET_GENERATOR_H

#include "minnet.h"
#include "jsutils.h"
#include <libwebsockets.h>
#include <pthread.h>
#include "minnet-buffer.h"

typedef struct generator {
  int ref_count;
  AsyncIterator iterator;
  MinnetBuffer buffer;
} MinnetGenerator;

void generator_zero(struct generator*);
void generator_free(struct generator*, JSRuntime*);
struct generator* generator_new(JSContext*);
JSValue minnet_generator_constructor(JSContext*, JSValueConst, int, JSValueConst argv[]);
JSValue minnet_generator_wrap(JSContext*, struct generator*);

extern JSClassDef minnet_generator_class;
extern THREAD_LOCAL JSValue minnet_generator_proto, minnet_generator_ctor;
extern THREAD_LOCAL JSClassID minnet_generator_class_id;
extern const JSCFunctionListEntry minnet_generator_proto_funcs[];
extern const size_t minnet_generator_proto_funcs_size;

static inline MinnetGenerator*
minnet_generator_data(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_generator_class_id);
}

#endif /* MINNET_GENERATOR_H */

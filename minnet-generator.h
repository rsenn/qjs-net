#ifndef MINNET_GENERATOR_H
#define MINNET_GENERATOR_H

#include <quickjs.h>
#include <cutils.h>
#include "minnet.h"
#include <libwebsockets.h>
#include <pthread.h>

typedef struct generator {
  size_t ref_count;
   AsyncIterator iterator;
} MinnetGenerator;

void generator_dump(struct generator const*);
void generator_init(struct generator*, size_t, size_t, const char* type, size_t typelen);
struct generator* generator_new(JSContext*);
struct generator* generator_new2(size_t, size_t, JSContext*);
size_t generator_insert(struct generator*, const void*, size_t);
size_t generator_consume(struct generator*, void*, size_t);
size_t generator_skip(struct generator*, size_t);
const void* generator_next(struct generator*);
size_t generator_size(struct generator*);
size_t generator_avail(struct generator*);
void generator_zero(struct generator*);
void generator_free(struct generator*, JSRuntime*);
JSValue minnet_generator_constructor(JSContext*, JSValue, int, JSValue argv[]);
JSValue minnet_generator_new(JSContext*, const char*, size_t, const void* x, size_t n);
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

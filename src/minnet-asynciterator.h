#ifndef MINNET_ASYNCITERATOR_H
#define MINNET_ASYNCITERATOR_H

#include <quickjs.h>
#include "asynciterator.h"
#include "utils.h"

JSValue minnet_asynciterator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]);
JSValue minnet_asynciterator_wrap(JSContext* ctx, AsyncIterator* iter);
void minnet_asynciterator_decorate(JSContext* ctx, JSValueConst this_val, JSValueConst ret);
JSValue minnet_asynciterator_iterable(JSContext* ctx, JSValueConst this_val);

extern THREAD_LOCAL JSClassID minnet_asynciterator_class_id;
extern THREAD_LOCAL JSValue minnet_asynciterator_proto, minnet_asynciterator_ctor;
extern JSClassDef minnet_asynciterator_class;
extern const JSCFunctionListEntry minnet_asynciterator_proto_funcs[];
extern const size_t minnet_asynciterator_proto_funcs_size;

static inline AsyncIterator*
minnet_asynciterator_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_asynciterator_class_id);
}

static inline AsyncIterator*
minnet_asynciterator_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_asynciterator_class_id);
}
#endif /* MINNET_ASYNCITERATOR_H */

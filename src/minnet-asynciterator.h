#ifndef MINNET_ASYNCITERATOR_H
#define MINNET_ASYNCITERATOR_H

#include <quickjs.h>
#include "asynciterator.h"
#include "utils.h"

void minnet_asynciterator_decorate(JSContext*, JSValueConst, JSValueConst);
JSValue minnet_asynciterator_constructor(JSContext*, JSValueConst, int, JSValueConst[]);
int minnet_asynciterator_init(JSContext*, JSModuleDef*);

extern THREAD_LOCAL JSClassID minnet_asynciterator_class_id;
extern THREAD_LOCAL JSValue minnet_asynciterator_proto, minnet_asynciterator_ctor;

static inline AsyncIterator*
minnet_asynciterator_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_asynciterator_class_id);
}

static inline AsyncIterator*
minnet_asynciterator_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_asynciterator_class_id);
}

#endif /* MINNET_ASYNCITERATOR_H */

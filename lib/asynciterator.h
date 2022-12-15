#ifndef QJSNET_LIB_ASYNCITERATOR_H
#define QJSNET_LIB_ASYNCITERATOR_H

#include "jsutils.h"

typedef struct async_read {
  struct list_head link;
  ResolveFunctions promise;
} AsyncRead;

typedef struct async_iterator {
  struct list_head reads;
  BOOL closed, closing;
} AsyncIterator;

void asynciterator_zero(AsyncIterator*);
void asynciterator_clear(AsyncIterator*, JSRuntime*);
AsyncIterator* asynciterator_new(JSContext*);
JSValue asynciterator_next(AsyncIterator*, JSContext*);
BOOL asynciterator_check_closing(AsyncIterator*, JSContext*);
int asynciterator_reject_all(AsyncIterator*, JSValueConst, JSContext*);
BOOL asynciterator_stop(AsyncIterator*, JSValueConst, JSContext*);
BOOL asynciterator_emplace(AsyncIterator* it, JSValueConst value, BOOL done, JSContext* ctx);
JSValue asynciterator_object(JSValueConst, BOOL, JSContext*);

static inline BOOL
asynciterator_yield(AsyncIterator* it, JSValueConst value, JSContext* ctx) {
  return list_empty(&it->reads) ? FALSE : asynciterator_emplace(it, value, FALSE, ctx);
}

#endif /* QJSNET_LIB_ASYNCITERATOR_H */

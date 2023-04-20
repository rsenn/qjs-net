#ifndef QJSNET_LIB_ASYNCITERATOR_H
#define QJSNET_LIB_ASYNCITERATOR_H

#include "jsutils.h"

typedef struct async_read {
  struct list_head link;
  ResolveFunctions promise;
  uint32_t id;
} AsyncRead;

typedef struct async_iterator {
  int ref_count;
  struct list_head reads;
  BOOL closed, closing;
  uint32_t serial;
} AsyncIterator;

void asynciterator_zero(AsyncIterator*);
void asynciterator_clear(AsyncIterator*, JSRuntime* rt);
JSValue asynciterator_next(AsyncIterator*, JSContext* ctx);
BOOL asynciterator_stop(AsyncIterator*, JSContext* ctx);
int asynciterator_cancel(AsyncIterator*, JSValueConst error, JSContext* ctx);
BOOL asynciterator_emplace(AsyncIterator*, JSValueConst value, BOOL done, JSContext* ctx);
JSValue asynciterator_object(JSValueConst, BOOL done, JSContext* ctx);

static inline BOOL
asynciterator_yield(AsyncIterator* it, JSValueConst value, JSContext* ctx) {
  return list_empty(&it->reads) ? FALSE : asynciterator_emplace(it, value, FALSE, ctx);
}

static inline AsyncIterator*
asynciterator_dup(AsyncIterator* it) {
  ++it->ref_count;
  return it;
}

#endif /* QJSNET_LIB_ASYNCITERATOR_H */

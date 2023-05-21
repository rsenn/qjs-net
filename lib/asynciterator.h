#ifndef QJSNET_LIB_ASYNCITERATOR_H
#define QJSNET_LIB_ASYNCITERATOR_H

#include "jsutils.h"

typedef struct async_read {
  struct list_head link;
  ResolveFunctions promise;
  JSValueConst argument;
  uint32_t id;
} AsyncRead;

typedef struct async_iterator {
  int ref_count;
  struct list_head reads;
  BOOL closed, closing;
  uint32_t serial;
} AsyncIterator;

size_t asynciterator_num_reads(AsyncIterator*);
void asynciterator_zero(AsyncIterator*);
void asynciterator_clear(AsyncIterator*, JSRuntime* rt);
AsyncIterator* asynciterator_new(JSContext*);
void asynciterator_free(AsyncIterator*, JSRuntime* rt);
JSValue asynciterator_next(AsyncIterator*, JSValueConst argument, JSContext* ctx);
BOOL asynciterator_stop(AsyncIterator*, JSValueConst value, JSContext* ctx);
int asynciterator_cancel(AsyncIterator*, JSValueConst error, JSContext* ctx);
BOOL asynciterator_emplace(AsyncIterator*, JSValueConst value, BOOL done, JSContext* ctx);

static inline BOOL
asynciterator_pending(AsyncIterator* it) {
  return !list_empty(&it->reads);
}

static inline AsyncRead*
asynciterator_front(AsyncIterator* it) {
  return list_entry(list_front(&it->reads), AsyncRead, link);
}

static inline BOOL
asynciterator_yield(AsyncIterator* it, JSValueConst value, JSContext* ctx) {
  if(list_empty(&it->reads))
    return FALSE;

  asynciterator_emplace(it, value, FALSE, ctx);
  return TRUE;
}

static inline BOOL
asynciterator_throw(AsyncIterator* it, JSValueConst error, JSContext* ctx) {
  if(list_empty(&it->reads))
    return FALSE;

  asynciterator_cancel(it, error, ctx);
  return TRUE;
}

static inline AsyncIterator*
asynciterator_dup(AsyncIterator* it) {
  ++it->ref_count;
  return it;
}

#endif /* QJSNET_LIB_ASYNCITERATOR_H */

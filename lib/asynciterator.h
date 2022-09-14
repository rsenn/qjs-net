#ifndef QJSNET_LIB_ASYNCITERATOR_H
#define QJSNET_LIB_ASYNCITERATOR_H

#include "jsutils.h"
#include "utils.h"

typedef struct async_read {
  struct list_head link;
  ResolveFunctions promise;
} AsyncRead;

typedef struct value_item {
  struct list_head link;
  JSValue value;
  int64_t id;
} ValueItem;

typedef struct async_iterator {
  JSContext* ctx;
  BOOL closed, closing;
  struct list_head reads;
} AsyncIterator;

void asynciterator_zero(AsyncIterator* it);
void asynciterator_clear(AsyncIterator* it, JSRuntime* rt);
AsyncIterator* asynciterator_new(JSContext* ctx);
JSValue asynciterator_next(AsyncIterator* it, JSContext* ctx);
BOOL asynciterator_check_closing(AsyncIterator* it, JSContext* ctx);
BOOL asynciterator_yield(AsyncIterator* it, JSValueConst value, JSContext* ctx);
int asynciterator_reject_all(AsyncIterator* it, JSValueConst value, JSContext* ctx);
BOOL asynciterator_stop(AsyncIterator* it, JSValueConst value, JSContext* ctx);
BOOL asynciterator_emplace(AsyncIterator* it, JSValueConst obj, JSContext* ctx);
JSValue asynciterator_obj(JSValueConst value, BOOL done, JSContext* ctx);

#endif /* QJSNET_LIB_ASYNCITERATOR_H */

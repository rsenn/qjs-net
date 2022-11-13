#ifndef QJSNET_LIB_ASYNCITERATOR_H
#define QJSNET_LIB_ASYNCITERATOR_H

#include "jsutils.h"

typedef struct async_read {
  struct list_head link;
  ResolveFunctions promise;
} AsyncRead;

typedef struct async_iterator {
  JSContext* ctx;
  BOOL closed, closing;
  struct list_head reads;
} AsyncIterator;

void asynciterator_zero(AsyncIterator*);
void asynciterator_clear(AsyncIterator*, JSRuntime*);
AsyncIterator* asynciterator_new(JSContext*);
JSValue asynciterator_next(AsyncIterator*, JSContext*);
BOOL asynciterator_check_closing(AsyncIterator*, JSContext*);
BOOL asynciterator_yield(AsyncIterator*, JSValueConst, JSContext*);
int asynciterator_reject_all(AsyncIterator*, JSValueConst, JSContext*);
BOOL asynciterator_stop(AsyncIterator*, JSValueConst, JSContext*);
BOOL asynciterator_emplace(AsyncIterator*, JSValueConst, JSContext*);
JSValue asynciterator_obj(JSValueConst, BOOL, JSContext*);

#endif /* QJSNET_LIB_ASYNCITERATOR_H */

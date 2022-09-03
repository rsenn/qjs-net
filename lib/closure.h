#ifndef QJSNET_LIB_CLOSURE_H
#define QJSNET_LIB_CLOSURE_H

#include <quickjs.h>
#include "allocated.h"

typedef union closure {
  struct {
    void* pointer;
    void (*free_func)();
    int ref_count;
    JSContext* ctx;
  };
  struct allocated allocated;
} MinnetClosure;

MinnetClosure* closure_new(JSContext*);
MinnetClosure* closure_dup(MinnetClosure*);
void closure_free(void*);

#endif /* QJSNET_LIB_CLOSURE_H */

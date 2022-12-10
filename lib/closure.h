#ifndef QJSNET_LIB_CLOSURE_H
#define QJSNET_LIB_CLOSURE_H

#include <quickjs.h>
#include "allocated.h"

typedef void closure_free_t(void*, JSRuntime*);

union closure {
  struct {
    void* pointer;
    void (*free_func)(void*, JSRuntime*);
    int ref_count;
    JSContext* ctx;
  };
  struct allocated allocated;
};

union closure* closure_new(JSContext*);
union closure* closure_dup(union closure*);
void closure_free(void*);

#endif /* QJSNET_LIB_CLOSURE_H */

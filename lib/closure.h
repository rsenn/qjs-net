#ifndef QJSNET_LIB_CLOSURE_H
#define QJSNET_LIB_CLOSURE_H

#include <quickjs.h>
#include "allocated.h"

typedef void closure_free_t(void*, JSRuntime*);

union closure {
  struct {
    void* pointer;
    JSRuntime* rt;
    closure_free_t* free_func;
    int ref_count;
  };
  struct allocated allocated;
};

union closure* closure_new(JSContext*);
union closure* closure_dup(union closure*);
void closure_free(void*);

#endif /* QJSNET_LIB_CLOSURE_H */

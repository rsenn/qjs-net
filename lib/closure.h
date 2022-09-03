#ifndef QJSNET_LIB_CLOSURE_H
#define QJSNET_LIB_CLOSURE_H

#include <quickjs.h>
#include "allocated.h"

struct context;
struct client_context;
struct server_context;

typedef struct closure {
  int ref_count;
  JSContext* ctx;
  union {
    struct {
      void* pointer;
      void (*free_func)(/*void**/);
    };
    struct allocated allocated;
  };
} MinnetClosure;

MinnetClosure* closure_new(JSContext*);
MinnetClosure* closure_dup(MinnetClosure*);
void closure_free(void*);

#endif /* QJSNET_LIB_CLOSURE_H */

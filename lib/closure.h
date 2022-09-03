#ifndef QUICKJS_NET_LIB_CLOSURE_H
#define QUICKJS_NET_LIB_CLOSURE_H

#include <quickjs.h>

struct context;
struct client_context;
struct server_context;

typedef struct closure {
  int ref_count;
  union {
    void* pointer;
    /* struct context* context;
     struct client_context* client;
     struct server_context* server;*/
  };
  void (*free_func)(/*void**/);
  JSContext* ctx;
} MinnetClosure;

MinnetClosure* closure_new(JSContext*);
MinnetClosure* closure_dup(MinnetClosure*);
void closure_free(void*);

#endif /* QUICKJS_NET_LIB_CLOSURE_H */

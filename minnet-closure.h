#ifndef MINNET_CLOSURE_H
#define MINNET_CLOSURE_H

#include <quickjs.h>

struct context;
struct client_context;
struct server_context;

typedef struct closure {
  int ref_count;
  union {
    struct context* context;
    struct client_context* client;
    struct server_context* server;
  };
  void (*free_func)();
} MinnetClosure;

MinnetClosure* closure_new(JSContext*);
MinnetClosure* closure_dup(MinnetClosure*);
void closure_free(void*);

#endif /* MINNET_CLOSURE_H */

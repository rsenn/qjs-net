#ifndef MINNET_CLIENT_H
#define MINNET_CLIENT_H

#include <quickjs.h>
#include "minnet.h"
#include "minnet-url.h"
#include "minnet-request.h"
#include "jsutils.h"

#define client_exception(client, retval) context_exception(&(client->context), (retval))

typedef struct client_context {
  MinnetContext context;
  MinnetCallbacks cb;
  JSValue headers, body, next;
  MinnetURL url;
  MinnetSession session;
  struct http_request* request;
  struct http_response* response;
  struct lws_client_connect_info connect_info;
  ResolveFunctions promise;
} MinnetClient;

void client_free(MinnetClient*);
JSValue minnet_client(JSContext*, JSValue, int, JSValue argv[]);
JSValue minnet_client_closure(JSContext*, JSValue, int, JSValue argv[], int magic, void* ptr);

struct client_closure {
  int ref_count;
  MinnetClient* client;
};

void client_closure_free(void*);
struct client_closure* client_closure_new(JSContext*);

static inline struct client_context*
lws_client(struct lws* wsi) {
  return lws_context_user(lws_get_context(wsi));
}
#endif

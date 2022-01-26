#ifndef MINNET_CLIENT_H
#define MINNET_CLIENT_H

#include <quickjs.h>
#include "minnet.h"
#include "minnet-url.h"
#include "minnet-request.h"

typedef struct client_context {
  MinnetContext context;
  MinnetCallbacks cb;
  JSValue headers, body, next;
  MinnetURL url;
  struct http_request* request;
  struct http_response* response;
  struct lws_client_connect_info connect_info;
} MinnetClient;

extern THREAD_LOCAL MinnetClient* minnet_client;

JSValue minnet_ws_client(JSContext*, JSValue, int, JSValue* argv);

static inline struct client_context*
lws_client(struct lws* wsi) {
  return lws_context_user(lws_get_context(wsi));
}
#endif

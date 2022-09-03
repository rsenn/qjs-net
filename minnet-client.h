#ifndef MINNET_CLIENT_H
#define MINNET_CLIENT_H

#include <libwebsockets.h> // for lws_context_user, lws_get_context, lws_cl...
#include <quickjs.h>       // for JSValue, JSValueConst, JSContext
#include <stdint.h>        // for uint8_t
#include "callback.h"      // for CallbackList
#include "context.h"       // for MinnetContext
#include "cutils.h"        // for BOOL
#include "jsutils.h"       // for ResolveFunctions
#include "session.h"       // for MinnetSession

#define client_exception(client, retval) context_exception(&(client->context), (retval))

typedef struct client_context {
  union {
    int ref_count;
    MinnetContext context;
  };
  struct lws* wsi;
  CallbackList on;
  JSValue headers, body, next;
  BOOL done;
  // MinnetURL url;
  MinnetSession session;
  struct http_request* request;
  struct http_response* response;
  struct lws_client_connect_info connect_info;
  ResolveFunctions promise;
} MinnetClient;

void client_certificate(MinnetContext*, JSValueConst);
void client_free(MinnetClient*);
void client_zero(MinnetClient*);
MinnetClient* client_dup(MinnetClient*);
JSValue minnet_client_closure(JSContext*, JSValueConst, int, JSValueConst argv[], int magic, void* ptr);
JSValue minnet_client(JSContext*, JSValueConst, int, JSValueConst argv[]);
uint8_t* scan_backwards(uint8_t*, uint8_t);

static inline struct client_context*
lws_client(struct lws* wsi) {
  return lws_context_user(lws_get_context(wsi));
}
#endif

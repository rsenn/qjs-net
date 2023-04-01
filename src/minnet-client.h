#ifndef MINNET_CLIENT_H
#define MINNET_CLIENT_H

#include <libwebsockets.h>
#include <quickjs.h>
#include <stdint.h>
#include "callback.h"
#include "context.h"
#include <cutils.h>
#include "jsutils.h"
#include "session.h"

#define client_exception(client, retval) context_exception(&(client->context), (retval))

typedef struct client_context {
  union {
    int ref_count;
    struct context context;
  };
  struct lws* wsi;
  CallbackList on;
  JSValue headers, body, next;
  BOOL done;
  struct session_data session;
  struct http_request* request;
  struct http_response* response;
  struct lws_client_connect_info connect_info;
  ResolveFunctions promise;
  struct list_head link;
  ByteBuffer recvb;
} MinnetClient;

void client_certificate(struct context*, JSValueConst options);
MinnetClient* client_new(JSContext*);
MinnetClient* client_find(struct lws*);
void client_free(MinnetClient*, JSContext* ctx);
void client_free_rt(MinnetClient*, JSRuntime* rt);
void client_zero(MinnetClient*);
MinnetClient* client_dup(MinnetClient*);
struct client_context* lws_client(struct lws*);
JSValue minnet_client_closure(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr);
JSValue minnet_client(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[]);
uint8_t* scan_backwards(uint8_t*, uint8_t ch);

#endif

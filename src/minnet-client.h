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
#include "asynciterator.h"
#include "generator.h"
#include "queue.h"

#define client_exception(client, retval) context_exception(&(client->context), (retval))

typedef struct client_context {
  union {
    int ref_count;
    struct context context;
  };
  struct lws* wsi;
  CallbackList on;
  JSValue body, next;
  struct session_data session;
  struct http_request* request;
  struct http_response* response;
  struct lws_client_connect_info connect_info;
  ResolveFunctions promise;
  union {
    AsyncIterator* iter;
    Generator* gen;
  };
  Queue* recvq;
  BOOL block;
} MinnetClient;

void client_certificate(struct context*, JSValueConst options);
MinnetClient* client_new(JSContext*);
void client_free(MinnetClient*, JSRuntime* rt);
void client_zero(MinnetClient*);
MinnetClient* client_dup(MinnetClient*);
Generator* client_generator(MinnetClient*, JSContext* ctx);
struct client_context* lws_client(struct lws*);
JSValue minnet_client_closure(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr);
JSValue minnet_client(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[]);
JSValue minnet_client_wrap(JSContext*, MinnetClient* client);

extern THREAD_LOCAL JSClassID minnet_client_class_id;
extern THREAD_LOCAL JSValue minnet_client_proto, minnet_client_ctor;
extern JSClassDef minnet_client_class;
extern const JSCFunctionListEntry minnet_client_proto_funcs[];
extern const size_t minnet_client_proto_funcs_size;

static inline MinnetClient*
minnet_client_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_client_class_id);
}

static inline MinnetClient*
minnet_client_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_client_class_id);
}

#endif

#ifndef MINNET_CLIENT_H
#define MINNET_CLIENT_H

#include <libwebsockets.h>
#include <quickjs.h>
#include <stdint.h>
#include "callback.h"
#include "context.h"
#include <cutils.h>
#include "js-utils.h"
#include "session.h"
#include "asynciterator.h"
#include "generator.h"
#include "queue.h"

#define minnet_client_exception(client, retval) context_exception(&(client->context), (retval))

typedef struct client_context {
  union {
    struct {
      int ref_count;
      JSContext* js;
      struct lws_context* lws;
      ResolveFunctions promise;
    };
    struct context context;
  };
  struct lws* wsi;
  CallbackList on;
  JSValue body, next;
  struct session_data session;
  struct http_request* request;
  struct http_response* response;
  struct lws_client_connect_info connect_info;
  union {
    AsyncIterator* iter;
    Generator* gen;
  };
  Queue* recvq;
  BOOL blocking, buffering, line_buffered, binary;
  size_t buf_size;
  int lwsret;
} MinnetClient;

enum {
  RETURN_CLIENT = 0,
  RETURN_RESPONSE,
};

void minnet_client_certificate(struct context*, JSValueConst);
MinnetClient* minnet_client_new(JSContext*);
void minnet_client_free(MinnetClient*, JSRuntime*);
void minnet_client_zero(MinnetClient*);
MinnetClient* minnet_client_dup(MinnetClient*);
Generator* minnet_client_generator(MinnetClient*, JSContext*);
struct client_context* lws_client(struct lws*);
JSValue minnet_client_closure(JSContext*, JSValueConst, int, JSValueConst[], int, void*);
JSValue minnet_client(JSContext*, JSValueConst, int, JSValueConst[]);
JSValue minnet_client_wrap(JSContext*, MinnetClient*);
int minnet_client_init(JSContext*, JSModuleDef*);

extern THREAD_LOCAL JSClassID minnet_client_class_id;
extern THREAD_LOCAL JSValue minnet_client_proto, minnet_client_ctor;

static inline MinnetClient*
minnet_client_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_client_class_id);
}

static inline MinnetClient*
minnet_client_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_client_class_id);
}

#endif

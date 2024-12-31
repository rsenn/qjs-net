#ifndef MINNET_SERVER_H
#define MINNET_SERVER_H

#include <quickjs.h>
#include "buffer.h"
#include "minnet.h"
#include "minnet-server-http.h"
#include "context.h"

#define minnet_server_exception(server, retval) context_exception(&((server)->context), (retval))

struct http_mount;

typedef struct {
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
  MinnetVhostOptions* mimetypes;
  BOOL listening;
} MinnetServer;

struct proxy_connection;

MinnetServer* minnet_server_dup(MinnetServer*);
void minnet_server_free(MinnetServer*);
JSValue minnet_server_match(MinnetServer*, const char*, enum http_method, JSValueConst callback, JSValueConst prev_callback);
void minnet_server_mounts(MinnetServer*, JSValueConst);
void minnet_server_certificate(struct context*, JSValueConst);
JSValue minnet_server_wrap(JSContext*, MinnetServer*);
JSValue minnet_server_method(JSContext*, JSValueConst, int, JSValueConst argv[], int magic);
JSValue minnet_server_closure(JSContext*, JSValueConst, int, JSValueConst argv[], int magic, void* ptr);
JSValue minnet_server(JSContext*, JSValueConst, int, JSValueConst argv[]);
int minnet_server_init(JSContext*, JSModuleDef*);
/*int defprot_callback(struct lws*, enum lws_callback_reasons, void*, void* in, size_t len);*/

extern THREAD_LOCAL JSClassID minnet_server_class_id;
extern THREAD_LOCAL JSValue minnet_server_proto, minnet_server_ctor;

static inline MinnetServer*
lws_server(struct lws* wsi) {
  return lws_context_user(lws_get_context(wsi));
}

static inline MinnetServer*
minnet_server_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_server_class_id);
}

static inline MinnetServer*
minnet_server_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_server_class_id);
}

#endif /* MINNET_SERVER_H */

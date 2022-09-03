#ifndef MINNET_SERVER_H
#define MINNET_SERVER_H

#include <quickjs.h>
#include "buffer.h"
#include "minnet.h"
#include "minnet-server-http.h"

#define server_exception(server, retval) context_exception(&((server)->context), (retval))

struct http_mount;

typedef struct server_context {
  union {
    int ref_count;
    MinnetContext context;
  };
  struct lws* wsi;
  MinnetCallbacks cb;
  MinnetVhostOptions* mimetypes;
  ResolveFunctions promise;
} MinnetServer;

struct proxy_connection;

void server_certificate(MinnetContext*, JSValue);
JSValue minnet_server(JSContext*, JSValue, int, JSValue argv[]);
int defprot_callback(struct lws*, enum lws_callback_reasons, void*, void* in, size_t len);
int ws_callback(struct lws*, enum lws_callback_reasons, void*, void* in, size_t len);

// extern THREAD_LOCAL MinnetServer minnet_server;

#endif /* MINNET_SERVER_H */

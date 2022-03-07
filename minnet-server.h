#ifndef MINNET_SERVER_H
#define MINNET_SERVER_H

#include <quickjs.h>
#include "minnet-buffer.h"
#include "minnet.h"

#define server_exception(server, retval) context_exception(&((server)->context), (retval))

struct http_mount;

typedef struct server_context {
  MinnetContext context;
  MinnetCallbacks cb;
  MinnetSession session;
} MinnetServer;

struct proxy_connection;

JSValue minnet_server(JSContext*, JSValue, int argc, JSValue* argv);
int proxy_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int raw_client_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int ws_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int defprot_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int http_server_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

// extern THREAD_LOCAL MinnetServer minnet_server;

#endif /* MINNET_SERVER_H */

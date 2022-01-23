#ifndef MINNET_SERVER_H
#define MINNET_SERVER_H

#include <quickjs.h>
#include "buffer.h"
#include "minnet.h"

struct http_mount;

typedef struct server_context {
  struct lws_context* lws;
  struct lws_context_creation_info info;
  JSContext* ctx;
  MinnetCallback cb_message, cb_connect, cb_close, cb_pong, cb_fd, cb_http;
} MinnetServer;

struct proxy_connection;

JSValue minnet_ws_server(JSContext*, JSValue, int argc, JSValue* argv);
int http_server_headers(JSContext*, MinnetBuffer*, struct lws* wsi);

extern MinnetServer minnet_server;

#endif /* MINNET_SERVER_H */

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
  MinnetCallback cb_message, cb_connect, cb_error, cb_close, cb_pong, cb_fd, cb_http;
} MinnetServer;

struct proxy_connection;

typedef struct session_data {
  JSValue ws_obj;
  union {
    struct {
      JSValue req_obj;
      JSValue resp_obj;
    };
    JSValue args[2];
  };
  struct http_mount* mount;
  struct proxy_connection* proxy;
  int serial;
  JSValue generator, next;
  BOOL done, responded;
  unsigned closed : 1;
  unsigned h2 : 1;
  int64_t written;
} MinnetSession;

JSValue minnet_ws_server(JSContext*, JSValue, int argc, JSValue* argv);
int http_headers(JSContext*, MinnetBuffer*, struct lws* wsi);

extern MinnetServer minnet_server;

#endif /* MINNET_SERVER_H */

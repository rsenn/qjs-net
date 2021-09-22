#ifndef MINNET_SERVER_H
#define MINNET_SERVER_H

#include <quickjs.h>
#include "minnet.h"
#include "minnet-response.h"
#include "minnet-request.h"

typedef struct http_mount {
  union {
    struct {
      struct http_mount* next;
      const char *mnt, *org, *def, *pro;
    };
    struct lws_http_mount lws;
  };
  MinnetCallback callback;
} MinnetHttpMount;

typedef struct http_server {
  struct lws_context* context;
  struct lws_context_creation_info info;
  JSContext* ctx;
  MinnetCallback cb_message, cb_connect, cb_close, cb_pong, cb_fd, cb_http;
} MinnetHttpServer;

typedef struct http_session {
  struct socket* ws;
  struct http_request* req;
  struct http_response* resp;
} MinnetHttpSession;

typedef struct server_context {
  JSValue ws_obj;
  union {
    struct {
      JSValue req_obj;
      JSValue resp_obj;
    };
    JSValue args[2];
  };
  struct http_mount* mount;
  size_t serial;
  JSValue generator;
  int closed : 1;
} MinnetServerContext;

JSValue minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
int http_writable(struct lws*, struct http_response*, BOOL done);
int http_callback(struct lws*, enum lws_callback_reasons, void* user, void* in, size_t len);

extern MinnetHttpServer minnet_server;

#endif /* MINNET_SERVER_H */

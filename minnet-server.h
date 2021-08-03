#ifndef MINNET_SERVER_H
#define MINNET_SERVER_H

#include <quickjs.h>
#include "minnet.h"
#include "minnet-response.h"
#include "minnet-request.h"

typedef struct http_mount {
  union {
    struct lws_http_mount lws;
    struct {
      struct http_mount* next;
      const char *mountpoint, *origin, *def, *protocol;
    };
  };
  MinnetCallback callback;
} MinnetHttpMount;

typedef struct http_server {
  struct lws_context* context;
  struct lws_context_creation_info info;
  JSContext* ctx;
  MinnetCallback cb_message, cb_connect, cb_close, cb_pong, cb_fd, cb_http, cb_body;
} MinnetHttpServer;

typedef struct http_session {
  struct lws* lwsi;
  JSValue request, response;
} MinnetHttpSession;

JSValue minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

#endif /* MINNET_SERVER_H */

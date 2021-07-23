#ifndef MINNET_SERVER_H
#define MINNET_SERVER_H

#include "minnet.h"
#include "minnet-response.h"

typedef struct lws_http_mount MinnetHttpMount;

typedef struct http_body {
  char path[128];
  size_t times, budget, content_lines;
} MinnetHttpBody;

typedef struct http_request {
  char *peer, *method, *uri;
  struct http_header header;
  struct http_body body;
  MinnetResponse response;
} MinnetHttpRequest;

JSValue minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

static int callback_ws(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
static int callback_http(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

#endif /* MINNET_SERVER_H */

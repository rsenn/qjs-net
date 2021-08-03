#ifndef MINNET_SERVER_H
#define MINNET_SERVER_H

#include "minnet.h"
#include "minnet-response.h"
#include "minnet-request.h"

typedef struct http_mount {
  union {
    struct lws_http_mount lws;
    struct {
      const struct http_mount* next;
      const char *mountpoint, *origin, *def, *protocol;
    };
  };
  MinnetCallback callback;
} MinnetHttpMount;

JSValue minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

#endif /* MINNET_SERVER_H */

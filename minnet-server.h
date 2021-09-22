#ifndef MINNET_SERVER_H
#define MINNET_SERVER_H

#include <quickjs.h>
#include "minnet.h"
#include "minnet-server-http.h"
 
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

extern MinnetHttpServer minnet_server;

#endif /* MINNET_SERVER_H */

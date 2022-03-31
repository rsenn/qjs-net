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
} MinnetServer;

struct proxy_connection;

JSValue minnet_ws_server(JSContext*, JSValue, int argc, JSValue* argv);

extern THREAD_LOCAL MinnetServer minnet_server;

#endif /* MINNET_SERVER_H */

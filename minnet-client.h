#ifndef MINNET_CLIENT_H
#define MINNET_CLIENT_H

#include <quickjs.h>
#include "minnet.h"
#include "minnet-url.h"
#include "minnet-request.h"

typedef struct {
  JSValue headers, body, next;
  MinnetStatus status : 8;
  MinnetURL url;
  struct lws_client_connect_info info;
  struct http_request* request;
  JSContext* ctx;
  MinnetCallback cb_message, cb_connect, cb_close, cb_pong, cb_fd, cb_http;
} MinnetClient;

JSValue minnet_ws_client(JSContext*, JSValue, int, JSValue* argv);

#endif

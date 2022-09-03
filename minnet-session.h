#ifndef MINNET_SESSION_H
#define MINNET_SESSION_H

#include <quickjs.h>
#include <stdint.h>
#include "minnet-buffer.h"

struct http_mount;
struct proxy_connection;
struct server_context;
struct client_context;

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
  JSValue generator, next;
  int serial;
  BOOL h2, in_body;
  int64_t written;
  struct server_context* server;
  struct client_context* client;
  MinnetBuffer send_buf;
} MinnetSession;

void session_zero(MinnetSession*);
void session_clear(MinnetSession*, JSContext*);
struct http_response* session_response(MinnetSession* session, MinnetCallback* cb);

#endif /* MINNET_SESSION_H */

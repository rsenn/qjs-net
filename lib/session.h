#ifndef QJSNET_LIB_SESSION_H
#define QJSNET_LIB_SESSION_H

#include "callback.h"
#include "queue.h"

struct http_mount;
struct proxy_connection;
struct server_context;
struct client_context;
struct wsi_opaque_user_data;

struct session_data {
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
  BOOL in_body, response_sent;
  struct server_context* server;
  struct client_context* client;
  Queue sendq;
};

// extern THREAD_LOCAL struct list_head session_list;

void session_zero(struct session_data*);
void session_clear(struct session_data*, JSContext* ctx);
void session_clear_rt(struct session_data*, JSRuntime* rt);
JSValue session_object(struct session_data* session, JSContext* ctx);
int session_writable(struct session_data*, BOOL binary, JSContext* ctx);

#endif /* QJSNET_LIB_SESSION_H */

#ifndef QJSNET_LIB_SESSION_H
#define QJSNET_LIB_SESSION_H

#include "callback.h"
#include "queue.h"

struct http_mount;
struct proxy_connection;
struct context;
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
  uint32_t wait_resolve, generator_run, callback_count;
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
int session_callback(struct session_data* session, JSCallback* cb, struct context*);
struct context* session_context(struct session_data* sess);

#define session_ws(sess) minnet_ws_data((sess)->ws_obj)
#define session_wsi(sess) session_ws(sess)->lwsi

struct wsi_opaque_user_data* session_opaque(struct session_data*);
struct context* session_context(struct session_data*);
struct http_request* session_request(struct session_data*);
struct http_response* session_response(struct session_data*);

#endif /* QJSNET_LIB_SESSION_H */

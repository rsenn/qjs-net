/**
 * @file session.h
 */
#ifndef QJSNET_LIB_SESSION_H
#define QJSNET_LIB_SESSION_H

#include "callback.h"
#include "queue.h"

struct http_mount;
struct proxy_connection;
struct context;
struct server_context;
struct wsi_opaque_user_data;

typedef enum { SYNC = 0, ASYNC = 1, GENERATOR = 2, ASYNC_GENERATOR = 3 } FunctionType;

struct session_data {
  JSValue ws_obj;
  union {
    struct {
      JSValue req_obj;
      JSValue resp_obj;
    };
    JSValue args[2];
  };
  struct context* context;
  struct http_mount* mount;
  struct proxy_connection* proxy;
  JSValue generator, next;
  BOOL in_body, response_sent, want_write;
  uint32_t wait_resolve, generator_run, callback_count;
  struct session_data** wait_resolve_ptr;
  Queue sendq;
  lws_callback_function* callback;
};

// extern THREAD_LOCAL struct list_head session_list;

void session_init(struct session_data* session, struct context* context);
void session_clear(struct session_data*, JSRuntime*);
JSValue session_object(struct session_data*, JSContext*);
void session_want_write(struct session_data*, struct lws*);
int session_writable(struct session_data*, struct lws*, JSContext*);
FunctionType session_callback(struct session_data*, JSCallback*);
FunctionType session_generator(struct session_data* session, JSValueConst, JSValueConst);

#endif /* QJSNET_LIB_SESSION_H */

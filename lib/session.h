#ifndef QJSNET_LIB_SESSION_H
#define QJSNET_LIB_SESSION_H

#include <stdint.h>
#include "buffer.h"
#include "callback.h"
#include <list.h>

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
  int serial;
  BOOL h2, in_body;
  int64_t written;
  struct server_context* server;
  struct client_context* client;
  ByteBuffer send_buf;
  struct list_head link;
};

extern THREAD_LOCAL struct list_head session_list;

void session_zero(struct session_data*);
void session_clear(struct session_data*, JSContext* ctx);
void session_clear_rt(struct session_data*, JSRuntime* rt);
JSValue session_object(struct wsi_opaque_user_data*, JSContext*);

#endif /* QJSNET_LIB_SESSION_H */

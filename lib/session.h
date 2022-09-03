#ifndef MINNET_SESSION_H
#define MINNET_SESSION_H

#include <stdint.h>
#include "buffer.h"
#include "callback.h"
#include <list.h>

struct http_mount;
struct proxy_connection;
struct server_context;
struct client_context;
struct wsi_opaque_user_data;

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
  struct list_head link;
} MinnetSession;

extern THREAD_LOCAL struct list_head session_list;

void session_zero(MinnetSession*);
void session_clear(MinnetSession*, JSContext*);
JSValue session_object(struct wsi_opaque_user_data*, JSContext*);
JSValue minnet_get_sessions(JSContext*, JSValueConst, int, JSValueConst argv[]);

#endif /* MINNET_SESSION_H */

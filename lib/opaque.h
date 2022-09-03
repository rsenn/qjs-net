#ifndef QJSNET_LIB_OPAQUE_H
#define QJSNET_LIB_OPAQUE_H

#include "utils.h"
#include <list.h>
#include <libwebsockets.h>

enum socket_state {
  CONNECTING = 0,
  OPEN = 1,
  CLOSING = 2,
  CLOSED = 3,
};

struct wsi_opaque_user_data {
  int ref_count;
  struct socket* ws;
  struct http_request* req;
  struct http_response* resp;
  struct session_data* sess;
  JSValue handler;
  int64_t serial;
  enum socket_state status;
  struct pollfd poll;
  int error;
  BOOL binary;
  struct list_head link;
  struct form_parser* form_parser;
  char* uri;
  size_t uri_len;
};

extern THREAD_LOCAL int64_t serial;

void opaque_clear_rt(struct wsi_opaque_user_data*, JSRuntime* rt);
void opaque_free_rt(struct wsi_opaque_user_data*, JSRuntime* rt);
void opaque_clear(struct wsi_opaque_user_data*, JSContext* ctx);
void opaque_free(struct wsi_opaque_user_data*, JSContext* ctx);
struct wsi_opaque_user_data* opaque_new(JSContext*);
struct wsi_opaque_user_data* lws_opaque(struct lws*, JSContext* ctx);

static inline struct wsi_opaque_user_data*
opaque_dup(struct wsi_opaque_user_data* opaque) {
  opaque->ref_count++;
  return opaque;
}
#endif /* QJSNET_LIB_OPAQUE_H */

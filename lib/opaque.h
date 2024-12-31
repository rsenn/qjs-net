/**
 * @file opaque.h
 */
#ifndef QJSNET_LIB_OPAQUE_H
#define QJSNET_LIB_OPAQUE_H

#include <list.h>
#include <libwebsockets.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include "utils.h"

enum socket_state {
  CONNECTING = 0,
  OPEN = 1,
  CLOSING = 2,
  CLOSED = 3,
};

struct socket;
struct http_request;
struct http_response;
struct session_data;
struct form_parser;

struct wsi_opaque_user_data {
  int ref_count;
  struct socket* ws;
  struct http_request* req;
  struct http_response* resp;
  struct session_data* sess;
  int64_t serial;
  enum socket_state status;
  struct pollfd poll;
  struct list_head link;
  struct form_parser* form_parser;
  struct lws* upstream;
  int fd;
  BOOL writable;
};

extern THREAD_LOCAL int64_t opaque_serial;
extern THREAD_LOCAL struct list_head opaque_list;

void opaque_clear(struct wsi_opaque_user_data*, JSRuntime* rt);
void opaque_free(struct wsi_opaque_user_data*, JSRuntime* rt);
struct wsi_opaque_user_data* opaque_new(JSContext*);
struct wsi_opaque_user_data* lws_opaque(struct lws*, JSContext* ctx);
bool opaque_valid(struct wsi_opaque_user_data* opaque);

static inline struct wsi_opaque_user_data*
opaque_dup(struct wsi_opaque_user_data* opaque) {
  ++opaque->ref_count;
  return opaque;
}

static inline struct wsi_opaque_user_data*
opaque_fromwsi(struct lws* wsi) {
  struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);
  assert(opaque);
  return opaque;
}

#endif /* QJSNET_LIB_OPAQUE_H */

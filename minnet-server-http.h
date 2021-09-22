#ifndef MINNET_SERVER_HTTP_H
#define MINNET_SERVER_HTTP_H

#include <quickjs.h>
#include "minnet.h"

struct http_request;
struct http_response;

typedef struct http_mount {
  union {
    struct {
      struct http_mount* next;
      const char *mnt, *org, *def, *pro;
    };
    struct lws_http_mount lws;
  };
  MinnetCallback callback;
} MinnetHttpMount;

typedef struct http_session {
  struct socket* ws;
  struct http_request* req;
  struct http_response* resp;
} MinnetHttpSession;

MinnetHttpMount* mount_create(JSContext*, const char*, const char* origin, const char* def, enum lws_mount_protocols origin_proto);
MinnetHttpMount* mount_new(JSContext*, JSValue);
struct http_mount* mount_find(const char*, size_t);
void mount_free(JSContext*, MinnetHttpMount const*);
int http_writable(struct lws*, struct http_response*, BOOL done);
int http_callback(struct lws*, enum lws_callback_reasons, void* user, void* in, size_t len);

extern MinnetServer minnet_server;

static inline int
is_h2(struct lws* wsi) {
  return lws_get_network_wsi(wsi) != wsi;
}

#endif /* MINNET_SERVER_HTTP_H */

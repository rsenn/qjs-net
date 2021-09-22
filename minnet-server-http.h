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

int http_writable(struct lws*, struct http_response*, BOOL done);
int minnet_http_callback(struct lws*, enum lws_callback_reasons, void* user, void* in, size_t len);

extern MinnetServer minnet_server;

#endif /* MINNET_SERVER_HTTP_H */

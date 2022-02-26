#ifndef MINNET_SERVER_HTTP_H
#define MINNET_SERVER_HTTP_H

#include <quickjs.h>
#include "minnet.h"

struct http_request;
struct http_response;

typedef union http_vhost_options {
  struct lws_protocol_vhost_options lws;
  struct {
    union http_vhost_options *next, *options;
    const char *name, *value;
  };

} MinnetVhostOptions;

typedef struct http_mount {
  union {
    struct {
      struct http_mount* next;
      const char *mnt, *org, *def, *pro;
      union http_vhost_options *cgienv, *extra_mimetypes, *interpret;
    };
    struct lws_http_mount lws;
  };
  MinnetCallback callback;
} MinnetHttpMount;

/*typedef struct http_session {
  struct socket* ws;
  struct http_request* req;
  struct http_response* resp;
} MinnetHttpSession;*/

MinnetVhostOptions* vhost_options_create(JSContext*, const char* name, const char* value);
void vhost_options_free(JSContext*, MinnetVhostOptions* vo);
MinnetVhostOptions* vhost_options_new(JSContext*, JSValueConst vhost_option);
MinnetHttpMount* mount_create(JSContext*, const char*, const char* origin, const char* def, enum lws_mount_protocols origin_proto);
MinnetHttpMount* mount_new(JSContext*, JSValueConst, const char* key);
struct http_mount* mount_find(const char*, size_t);
void mount_free(JSContext*, MinnetHttpMount const*);
int http_server_writable(struct lws*, struct http_response*, BOOL done);
int http_server_callback(struct lws*, enum lws_callback_reasons, void* user, void* in, size_t len);

static inline int
is_h2(struct lws* wsi) {
  return lws_get_network_wsi(wsi) != wsi;
}

#endif /* MINNET_SERVER_HTTP_H */

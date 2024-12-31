#ifndef MINNET_SERVER_HTTP_H
#define MINNET_SERVER_HTTP_H

#include <quickjs.h>
#include "minnet.h"
#include "session.h"

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
  JSCallback callback;
} MinnetHttpMount;

MinnetVhostOptions* vhost_options_create(JSContext*, const char*, const char*);
MinnetVhostOptions* vhost_options_new(JSContext*, JSValue);
MinnetVhostOptions* vhost_options_fromobj(JSContext* ctx, JSValueConst obj);

void vhost_options_dump(MinnetVhostOptions* vo);
void vhost_options_free_list(JSContext*, MinnetVhostOptions*);
void vhost_options_free(JSContext*, MinnetVhostOptions*);
MinnetHttpMount* mount_new(JSContext*, const char*, const char*, const char* def, const char* pro);
MinnetHttpMount* mount_fromobj(JSContext*, JSValue, const char*);
struct http_mount* mount_find(MinnetHttpMount*, const char*, size_t);
struct http_mount* mount_find_s(MinnetHttpMount*, const char*);
void mount_fromvalue(JSContext* ctx, MinnetHttpMount** m, JSValueConst opt_mounts);
void mount_free(JSContext*, MinnetHttpMount const*);
BOOL mount_is_proxy(MinnetHttpMount const* m);
int minnet_http_server_callback(struct lws*, enum lws_callback_reasons, void*, void* in, size_t len);

#endif /* MINNET_SERVER_HTTP_H */

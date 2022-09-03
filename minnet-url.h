#ifndef MINNET_URL_H
#define MINNET_URL_H

#include <libwebsockets.h>
#include <quickjs.h>
#include <cutils.h>
#include <stdint.h>
#include "utils.h"

typedef enum protocol {
  PROTOCOL_WS = 0,
  PROTOCOL_WSS,
  PROTOCOL_HTTP,
  PROTOCOL_HTTPS,
  PROTOCOL_RAW,
  PROTOCOL_TLS,
} MinnetProtocol;

MinnetProtocol protocol_number(const char*);
const char* protocol_string(MinnetProtocol);
uint16_t protocol_default_port(MinnetProtocol);
BOOL protocol_is_tls(MinnetProtocol);

typedef struct url {
  int ref_count;
  const char* protocol;
  char *host, *path;
  uint16_t port;
} MinnetURL;

#define URL_INIT() \
  (MinnetURL) { 1, 0, 0, 0, 0 }

MinnetProtocol protocol_number(const char*);
const char* protocol_string(MinnetProtocol);
uint16_t protocol_default_port(MinnetProtocol);
BOOL protocol_is_tls(MinnetProtocol);
void url_init(MinnetURL*, const char*, const char*, int port, const char* path, JSContext* ctx);
void url_parse(MinnetURL*, const char*, JSContext*);
MinnetURL url_create(const char*, JSContext*);
size_t url_print(char*, size_t, const MinnetURL);
char* url_format(const MinnetURL, JSContext*);
size_t url_length(const MinnetURL);
void url_free(MinnetURL*, JSContext*);
void url_free_rt(MinnetURL*, JSRuntime*);
MinnetProtocol url_set_protocol(MinnetURL*, const char*);
BOOL url_set_path_len(MinnetURL*, const char*, size_t, JSContext*);
BOOL url_set_query_len(MinnetURL*, const char*, size_t, JSContext*);
void url_info(const MinnetURL, struct lws_client_connect_info*);
char* url_location(const MinnetURL, JSContext*);
const char* url_query(const MinnetURL);
void url_fromobj(MinnetURL*, JSValueConst, JSContext*);
BOOL url_fromvalue(MinnetURL*, JSValueConst, JSContext*);
void url_fromwsi(MinnetURL*, struct lws*, JSContext*);
void url_dump(const char*, MinnetURL const*);
JSValue minnet_url_wrap(JSContext*, MinnetURL*);
MinnetURL* url_new(JSContext*);
JSValue minnet_url_new(JSContext*, MinnetURL);
JSValue minnet_url_method(JSContext*, JSValueConst, int, JSValueConst argv[], int magic);
JSValue minnet_url_from(JSContext*, JSValueConst, int, JSValueConst argv[]);
JSValue minnet_url_inspect(JSContext*, JSValueConst, int, JSValueConst argv[]);
JSValue minnet_url_constructor(JSContext*, JSValueConst, int, JSValueConst argv[]);
int minnet_url_init(JSContext*, JSModuleDef*);

extern THREAD_LOCAL JSClassID minnet_url_class_id;

static inline const char*
url_path(const MinnetURL url) {
  return url.path;
}

static inline BOOL
url_valid(const MinnetURL url) {
  return url.host || url.path;
}

static inline MinnetProtocol
url_protocol(const MinnetURL url) {
  return protocol_number(url.protocol);
}

static inline MinnetURL*
url_dup(MinnetURL* url) {
  ++url->ref_count;
  return url;
}

static inline MinnetURL
url_clone(MinnetURL url, JSContext* ctx) {
  return (MinnetURL){
      .ref_count = 1,
      .protocol = url.protocol,
      .host = url.host ? js_strdup(ctx, url.host) : 0,
      .path = url.path ? js_strdup(ctx, url.path) : 0,
      .port = url.port,
  };
}

static inline void
url_copy(MinnetURL* url, const MinnetURL other, JSContext* ctx) {
  url->protocol = other.protocol;
  url->host = other.host ? js_strdup(ctx, other.host) : 0;
  url->path = other.path ? js_strdup(ctx, other.path) : 0;
  url->port = other.port;
}

static inline char*
url_string(MinnetURL const* url) {
  static char buf[4096];

  url_print(buf, sizeof(buf), *url);
  return buf;
}

int minnet_url_init(JSContext*, JSModuleDef* m);

static inline MinnetURL*
minnet_url_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_url_class_id);
}

static inline MinnetURL*
minnet_url_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_url_class_id);
}

static inline BOOL
url_set_path(MinnetURL* url, const char* path, JSContext* ctx) {
  return url_set_path_len(url, path, strlen(path), ctx);
}

static inline BOOL
url_set_query(MinnetURL* url, const char* query, JSContext* ctx) {
  return url_set_query_len(url, query, strlen(query), ctx);
}
#endif /* MINNET_URL_H */

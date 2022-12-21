#ifndef QJSNET_LIB_URL_H
#define QJSNET_LIB_URL_H

#include <libwebsockets.h>
#include <quickjs.h>
#include <cutils.h>
#include <stdint.h>
#include "utils.h"

enum protocol {
  PROTOCOL_WS = 0,
  PROTOCOL_WSS,
  PROTOCOL_HTTP,
  PROTOCOL_HTTPS,
  PROTOCOL_RAW,
  PROTOCOL_TLS,
  NUM_PROTOCOLS,
};

enum protocol protocol_number(const char*);
const char* protocol_string(enum protocol);
uint16_t protocol_default_port(enum protocol);
BOOL protocol_is_tls(enum protocol);

struct url {
  int ref_count;
  const char* protocol;
  char *host, *path;
  int port;
};

#define URL_INIT() \
  (struct url) { 1, 0, 0, 0, 0 }

enum protocol protocol_number(const char*);
const char* protocol_string(enum protocol);
uint16_t protocol_default_port(enum protocol);
BOOL protocol_is_tls(enum protocol);
void url_init(struct url*, const char*, const char*, int port, const char* path, JSContext* ctx);
void url_parse(struct url*, const char*, JSContext*);
struct url url_create(const char*, JSContext*);
size_t url_print(char*, size_t, const struct url);
char* url_format(const struct url, JSContext*);
size_t url_length(const struct url);
void url_free(struct url*, JSContext*);
void url_free_rt(struct url*, JSRuntime*);
enum protocol url_set_protocol(struct url*, const char*);
BOOL url_set_path_len(struct url*, const char*, size_t, JSContext*);
BOOL url_set_query_len(struct url*, const char*, size_t, JSContext*);
void url_info(const struct url, struct lws_client_connect_info*);
char* url_location(const struct url, JSContext*);
const char* url_query(const struct url);
void url_fromobj(struct url*, JSValueConst, JSContext*);
BOOL url_fromvalue(struct url*, JSValueConst, JSContext*);
void url_fromwsi(struct url*, struct lws*, JSContext*);
void url_dump(const char*, struct url const*);
struct url* url_new(JSContext*);

static inline const char*
url_path(const struct url url) {
  return url.path;
}

static inline BOOL
url_valid(const struct url url) {
  return url.host || url.path;
}

static inline enum protocol
url_protocol(const struct url url) {
  return protocol_number(url.protocol);
}

static inline struct url*
url_dup(struct url* url) {
  ++url->ref_count;
  return url;
}

static inline struct url
url_clone(struct url url, JSContext* ctx) {
  return (struct url){
      .ref_count = 1,
      .protocol = url.protocol,
      .host = url.host ? js_strdup(ctx, url.host) : 0,
      .path = url.path ? js_strdup(ctx, url.path) : 0,
      .port = url.port,
  };
}

static inline void
url_copy(struct url* url, const struct url other, JSContext* ctx) {
  url->protocol = other.protocol;
  url->host = other.host ? js_strdup(ctx, other.host) : 0;
  url->path = other.path ? js_strdup(ctx, other.path) : 0;
  url->port = other.port;
}

static inline char*
url_string(struct url const* url) {
  static char buf[4096];

  url_print(buf, sizeof(buf), *url);
  return buf;
}

static inline BOOL
url_set_path(struct url* url, const char* path, JSContext* ctx) {
  return url_set_path_len(url, path, strlen(path), ctx);
}

static inline BOOL
url_set_query(struct url* url, const char* query, JSContext* ctx) {
  return url_set_query_len(url, query, strlen(query), ctx);
}
#endif /* QJSNET_LIB_URL_H */

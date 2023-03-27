#ifndef QJSNET_LIB_URL_H
#define QJSNET_LIB_URL_H

#include <quickjs.h>
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

#define URL_IS_VALID_PORT(num) ((num) >= 0 && (num) <= 65535)

enum protocol protocol_number(const char*);
const char* protocol_string(enum protocol);
uint16_t protocol_default_port(enum protocol);
BOOL protocol_is_tls(enum protocol);

typedef struct url {
  int ref_count;
  const char* protocol;
  char *host, *path;
  int port;
} URL;

#define URL_INIT() \
  (struct url) { 1, 0, 0, 0, 0 }

enum protocol protocol_number(const char*);
const char*   protocol_string(enum protocol);
uint16_t      protocol_default_port(enum protocol);
BOOL          protocol_is_tls(enum protocol);

void          url_init(URL*, const char* protocol, const char* host, int port, const char* path, JSContext* ctx);
void          url_parse(URL*, const char* u, JSContext* ctx);
size_t        url_print(char*, size_t size, const URL url);
char*         url_format(const URL, JSContext* ctx);
char*         url_host(const URL, JSContext* ctx);
size_t        url_length(const URL);
void          url_free(URL*, JSContext* ctx);
void          url_free_rt(URL*, JSRuntime* rt);
enum protocol url_set_protocol(URL*, const char* proto);
BOOL          url_set_path_len(URL*, const char* path, size_t len, JSContext* ctx);
BOOL          url_set_query_len(URL*, const char* query, size_t len, JSContext* ctx);
void          url_info(const URL, struct lws_client_connect_info* info);
const char*   url_query(const URL);
const char*   url_search(const URL, size_t* len_p);
const char*   url_hash(const URL);
void          url_fromobj(URL*, JSValueConst obj, JSContext* ctx);
BOOL          url_fromvalue(URL*, JSValueConst value, JSContext* ctx);
void          url_fromwsi(URL*, struct lws* wsi, JSContext* ctx);
URL*          url_new(JSContext*);
JSValue       url_object(const URL, JSContext* ctx);

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

static inline BOOL
url_is_tls(const struct url url) {
  return url.protocol ? protocol_is_tls(protocol_number(url.protocol)) : FALSE;
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

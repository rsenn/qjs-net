#ifndef MINNET_URL_H
#define MINNET_URL_H

#include <libwebsockets.h>
#include <quickjs.h>
#include <stdint.h>
#include "minnet.h"

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
  const char* protocol;
  char *host, *path;
  uint16_t port;
} MinnetURL;

MinnetProtocol protocol_number(const char*);
const char*    protocol_string(MinnetProtocol);
uint16_t       protocol_default_port(MinnetProtocol);
BOOL           protocol_is_tls(MinnetProtocol);
void           url_init(MinnetURL*, const char*, const char*, int port, const char* path, JSContext* ctx);
void           url_parse(MinnetURL*, const char*, JSContext*);
char*          url_format(const MinnetURL*, JSContext*);
size_t         url_length(const MinnetURL);
MinnetProtocol url_set_protocol(MinnetURL*, const char*);
void           url_free(MinnetURL*, JSContext*);
void           url_free_rt(MinnetURL*, JSRuntime*);
void           url_info(const MinnetURL*, struct lws_client_connect_info*);
char*          url_location(const MinnetURL*, JSContext*);
const char*    url_query(const MinnetURL*);
void           url_from(MinnetURL*, JSValue, JSContext*);
JSValue        query_object(const char*, JSContext*);
char*          query_from(JSValue, JSContext*);
JSValue        minnet_url_new(JSContext*, MinnetURL);
JSValue        minnet_url_method(JSContext*, JSValue, int, JSValue argv[], int magic);
JSValue        minnet_url_from(JSContext*, JSValue, int, JSValue argv[]);
JSValue        minnet_url_constructor(JSContext*, JSValue, int, JSValue argv[]);
int            minnet_url_init(JSContext*, JSModuleDef*);

static inline MinnetURL
url_create(const char* s, JSContext* ctx) {
  MinnetURL url = {0, 0, 0, 0};
  url_parse(&url, s, ctx);
  return url;
}

/*extern THREAD_LOCAL JSClassID minnet_url_class_id;
extern THREAD_LOCAL JSValue minnet_url_proto, minnet_url_ctor;
extern JSClassDef minnet_url_class;
extern const JSCFunctionListEntry minnet_url_proto_funcs[], minnet_url_static_funcs[], minnet_url_proto_defs[];
extern const size_t minnet_url_proto_funcs_size, minnet_url_static_funcs_size, minnet_url_proto_defs_size;

*/
JSValue minnet_url_constructor(JSContext*, JSValue new_target, int argc, JSValue argv[]);

static inline const char*
url_path(const MinnetURL* url) {
  return url->path;
}

static inline MinnetProtocol
url_protocol(const MinnetURL* url) {
  return protocol_number(url->protocol);
}

extern THREAD_LOCAL JSClassID minnet_url_class_id;

int minnet_url_init(JSContext*, JSModuleDef* m);

static inline MinnetURL*
minnet_url_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_url_class_id);
}

static inline MinnetURL*
minnet_url_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_url_class_id);
}

static inline MinnetURL
url_clone(MinnetURL url, JSContext* ctx) {
  return (MinnetURL){
      .protocol = url.protocol,
      .host = url.host ? js_strdup(ctx, url.host) : 0,
      .path = url.path ? js_strdup(ctx, url.path) : 0,
      .port = url.port,
  };
}
JSValue minnet_url_new(JSContext* ctx, MinnetURL u);
static inline void
url_copy(MinnetURL* url, const MinnetURL other, JSContext* ctx) {
  url->protocol = other.protocol;
  url->host = other.host ? js_strdup(ctx, other.host) : 0;
  url->path = other.path ? js_strdup(ctx, other.path) : 0;
  url->port = other.port;
}

static inline BOOL
url_valid(const MinnetURL url) {
  return url.host && url.path;
}

#endif /* MINNET_URL_H */

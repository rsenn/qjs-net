#ifndef MINNET_URL_H
#define MINNET_URL_H

#include <libwebsockets.h>
#include <quickjs.h>
#include <stdint.h>
#include "minnet.h"

typedef struct url {
  char *protocol, *host, *path;
  uint16_t port;
} MinnetURL;

MinnetURL url_init(JSContext*, const char* protocol, const char* host, uint16_t port, const char* path);
MinnetURL url_parse(JSContext*, const char* url);
char* url_format(const MinnetURL*, JSContext* ctx);
void url_free(JSContext*, MinnetURL* url);
int url_connect(MinnetURL*, struct lws_context* context, struct lws** p_wsi);
char* url_location(const MinnetURL*, JSContext* ctx);
const char* url_query_string(const MinnetURL*);
JSValue url_query_object(const MinnetURL*, JSContext* ctx);
char* url_query_from(JSContext*, JSValue obj);

extern THREAD_LOCAL JSClassID minnet_url_class_id;
extern THREAD_LOCAL JSValue minnet_url_proto, minnet_url_ctor;
extern JSClassDef minnet_url_class;
extern const JSCFunctionListEntry minnet_url_proto_funcs[], minnet_url_static_funcs[], minnet_url_proto_defs[];
extern const size_t minnet_url_proto_funcs_size, minnet_url_static_funcs_size, minnet_url_proto_defs_size;

JSValue minnet_url_constructor(JSContext*, JSValue new_target, int argc, JSValue argv[]);

static inline const char*
url_path(const MinnetURL*url) {
  return url->path;
}

static inline MinnetURL*
minnet_url_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_url_class_id);
}

static inline MinnetURL*
minnet_url_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_url_class_id);
}

#endif /* MINNET_URL_H */

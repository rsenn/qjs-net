#ifndef MINNET_URL_H
#define MINNET_URL_H

#include <libwebsockets.h>
#include <quickjs.h>
#include <stdint.h>

typedef struct url {
  char *protocol, *host, *path;
  uint16_t port;
} MinnetURL;

MinnetURL url_init(JSContext*, const char* protocol, const char* host, uint16_t port, const char* path);
MinnetURL url_parse(JSContext*, const char* url);
char* url_format(const MinnetURL*, JSContext* ctx);
void url_free(JSContext*, MinnetURL* url);
int url_connect(MinnetURL*, struct lws_context* context, struct lws** p_wsi);
char* url_path(const MinnetURL*, JSContext* ctx);
const char* url_query_string(const MinnetURL*);
JSValue url_query_object(const MinnetURL*, JSContext* ctx);
char* url_query_from(JSContext*, JSValue obj);

#endif /* MINNET_URL_H */

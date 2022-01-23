#ifndef MINNET_URL_H
#define MINNET_URL_H

#include <libwebsockets.h>
#include <quickjs.h>
#include <stdint.h>

typedef struct url {
  char *protocol, *host, *path;
  uint16_t port;
} MinnetURL;

MinnetURL url_init(JSContext*, const char*, const char*, uint16_t port, const char* path);
MinnetURL url_parse(JSContext*, const char*);
char* url_format(const MinnetURL*, JSContext*);
void url_free(JSContext*, MinnetURL*);
int url_connect(MinnetURL*, struct lws_context*, struct lws**);

#endif /* MINNET_URL_H */

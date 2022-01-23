#ifndef MINNET_URL_H
#define MINNET_URL_H

#include <libwebsockets.h>

MinnetURL url_init(JSContext*, const char*, const char*, uint16_t port, const char* location);
MinnetURL url_parse(JSContext*, const char*);
void      url_free(JSContext*, MinnetURL*);
int       url_connect(MinnetURL*, struct lws_context*, struct lws**);

#endif /* MINNET_URL_H */

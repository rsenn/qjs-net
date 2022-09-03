#ifndef MINNET_CLIENT_HTTP_H
#define MINNET_CLIENT_HTTP_H

#include "minnet-request.h"
#include <libwebsockets.h>

int http_client_callback(struct lws*, enum lws_callback_reasons reason, void* user, void* in, size_t len);

#endif /* MINNET_CLIENT_HTTP_H */

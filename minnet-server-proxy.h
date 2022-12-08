#ifndef MINNET_SERVER_PROXY_H
#define MINNET_SERVER_PROXY_H

#include <quickjs.h>
#include "minnet.h"

enum { ACCEPTED = 0, ONWARD };

/*typedef struct proxy_msg {
  lws_dll2_t list;
  size_t len;
} MinnetProxyMessage;*/

typedef struct proxy_connection {
  struct lws* wsi[2];
  lws_dll2_owner_t queue[2];
} MinnetProxyConnection;

int proxy_ws_raw_msg_destroy(struct lws_dll2*, void*);
int proxy_server_callback(struct lws*, enum lws_callback_reasons, void*, void* in, size_t len);
int proxy_rawclient_callback(struct lws*, enum lws_callback_reasons, void*, void* in, size_t len);

#endif /* MINNET_SERVER_PROXY_H */

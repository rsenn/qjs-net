#ifndef MINNET_SERVER_H
#define MINNET_SERVER_H

#include "minnet.h"
#include "minnet-response.h"
#include "minnet-request.h"

typedef struct lws_http_mount MinnetHttpMount;

JSValue minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

#endif /* MINNET_SERVER_H */

#ifndef MINNET_CLIENT_H
#define MINNET_CLIENT_H

#include "quickjs.h"
#include <libwebsockets.h>
/*
extern struct minnet_ws_callback client_cb_message;
extern struct minnet_ws_callback client_cb_connect;
extern struct minnet_ws_callback client_cb_error;
extern struct minnet_ws_callback client_cb_close;
extern struct minnet_ws_callback client_cb_pong;
extern struct minnet_ws_callback client_cb_fd;*/

JSValue minnet_ws_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

#endif
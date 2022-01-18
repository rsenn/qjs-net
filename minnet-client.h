#ifndef MINNET_CLIENT_H
#define MINNET_CLIENT_H

#include <quickjs.h>
#include "minnet.h"
#include "buffer.h"

typedef struct client_data {
  JSValue ws_obj;
  struct byte_buffer body;
  BOOL connected;
} MinnetClient;

JSValue minnet_ws_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

#endif

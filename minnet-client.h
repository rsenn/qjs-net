#ifndef MINNET_CLIENT_H
#define MINNET_CLIENT_H

#include <quickjs.h>
#include "minnet.h"
#include "minnet-url.h"
#include "minnet-request.h"

typedef struct {
  MinnetURL url;
  MinnetRequest* request;
} MinnetClient;

JSValue minnet_ws_client(JSContext*, JSValue, int, JSValue* argv);

#endif

#ifndef MINNET_CLIENT_H
#define MINNET_CLIENT_H

#include <quickjs.h>
#include "minnet.h"

JSValue minnet_ws_client(JSContext*, JSValue, int, JSValue* argv);

#endif

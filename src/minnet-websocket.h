#ifndef MINNET_WEBSOCKET_H
#define MINNET_WEBSOCKET_H

#include "ws.h"

typedef struct socket MinnetWebsocket;

MinnetWebsocket* minnet_ws_data(JSValueConst);
/*JSValue minnet_ws_new(JSContext*, struct lws*);*/
JSValue minnet_ws_wrap(JSContext*, MinnetWebsocket*);
JSValue minnet_ws_fromwsi(JSContext*, struct lws*);
JSValue minnet_ws_constructor(JSContext*, JSValueConst, int, JSValueConst[]);
int minnet_ws_init(JSContext*, JSModuleDef*);

extern THREAD_LOCAL JSClassID minnet_ws_class_id;
extern THREAD_LOCAL JSValue minnet_ws_proto, minnet_ws_ctor;

static inline MinnetWebsocket*
minnet_ws_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_ws_class_id);
}
#endif /* MINNET_WEBSOCKET_H */

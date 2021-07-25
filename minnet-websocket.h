#ifndef MINNET_WEBSOCKET_H
#define MINNET_WEBSOCKET_H

#include <quickjs.h>

struct lws;

/* class WebSocket */

typedef struct {
  struct lws* lwsi;
  size_t ref_count;
} MinnetWebsocket;

JSValue minnet_ws_object(JSContext*, struct lws* wsi);
void minnet_ws_sslcert(JSContext*, struct lws_context_creation_info* info, JSValue options);

extern JSValue minnet_ws_proto;
extern JSClassDef minnet_ws_class;
extern const JSCFunctionListEntry minnet_ws_proto_funcs[];
extern const size_t minnet_ws_proto_funcs_size;
extern JSClassID minnet_ws_class_id;

#endif /* MINNET_WEBSOCKET_H */
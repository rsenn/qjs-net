#ifndef MINNET_WEBSOCKET_H
#define MINNET_WEBSOCKET_H
 
#include "lib/ws.h"
#include "minnet.h"
#include "minnet-ringbuffer.h"
#include "opaque.h"
#include <quickjs.h>
 
typedef struct socket  MinnetWebsocket;

JSValue minnet_ws_new(JSContext*, struct lws*); 
struct wsi_opaque_user_data* lws_opaque(struct lws*, JSContext*);
JSValue minnet_ws_wrap(JSContext*, MinnetWebsocket*);
JSValue minnet_ws_fromwsi(JSContext*, struct lws*);
JSValue minnet_ws_constructor(JSContext*, JSValue, int, JSValue argv[]);

extern THREAD_LOCAL JSClassID minnet_ws_class_id;
extern THREAD_LOCAL JSValue minnet_ws_proto, minnet_ws_ctor;
extern JSClassDef minnet_ws_class;
extern const JSCFunctionListEntry minnet_ws_proto_funcs[], minnet_ws_static_funcs[], minnet_ws_proto_defs[];
extern const size_t minnet_ws_proto_funcs_size, minnet_ws_static_funcs_size, minnet_ws_proto_defs_size;

static inline MinnetWebsocket*
minnet_ws_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_ws_class_id);
}

static inline MinnetWebsocket*
minnet_ws_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_ws_class_id);
}
#endif /* MINNET_WEBSOCKET_H */

#ifndef MINNET_WEBSOCKET_H
#define MINNET_WEBSOCKET_H

#include "quickjs.h"

struct lws;
struct http_header;

/* class WebSocket */

typedef struct {
  struct lws* lwsi;
  size_t ref_count;
  struct http_header* header;
} MinnetWebsocket;

void minnet_ws_sslcert(JSContext*, struct lws_context_creation_info* info, JSValue options);

extern JSClassDef minnet_ws_class;
extern const JSCFunctionListEntry minnet_ws_proto_funcs[];
extern const size_t minnet_ws_proto_funcs_size;
extern JSClassID minnet_ws_class_id;

typedef struct minnet_ws_callback {
  JSContext* ctx;
  JSValueConst* this_obj;
  JSValue* func_obj;
} MinnetWebsocketCallback;

#define GETCB(opt, cb_ptr)                                                                                                     \
  if(JS_IsFunction(ctx, opt)) {                                                                                                \
    MinnetWebsocketCallback cb = {ctx, &this_val, &opt};                                                                       \
    cb_ptr = cb;                                                                                                               \
  }

JSValue minnet_ws_object(JSContext* ctx, struct lws* wsi);

static inline JSValue
minnet_ws_emit(MinnetWebsocketCallback* cb, int argc, JSValue* argv) {
  if(!cb->func_obj)
    return JS_UNDEFINED;
  return JS_Call(cb->ctx, *(cb->func_obj), *(cb->this_obj), argc, argv);
}

#endif /* MINNET_WEBSOCKET_H */
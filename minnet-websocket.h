#ifndef MINNET_WEBSOCKET_H
#define MINNET_WEBSOCKET_H

#include "quickjs.h"
#include <libwebsockets.h>

struct lws;
struct byte_buffer;

/* class WebSocket */

typedef struct {
  struct lws* lwsi;
  size_t ref_count;
} MinnetWebsocket;

int lws_ws_callback(struct lws*, enum lws_callback_reasons reason, void* user, void* in, size_t len);
JSValue minnet_ws_object(JSContext*, struct lws* wsi);
JSValue minnet_ws_emit(struct callback_ws*, int argc, JSValue* argv);
void minnet_ws_sslcert(JSContext*, struct lws_context_creation_info* info, JSValue options);

extern JSClassDef minnet_ws_class;
extern const JSCFunctionListEntry minnet_ws_proto_funcs[];
extern const size_t minnet_ws_proto_funcs_size;
extern JSClassID minnet_ws_class_id;

#define GETCB(opt, cb_ptr)                                                                                                                                                                             \
  if(JS_IsFunction(ctx, opt)) {                                                                                                                                                                        \
    MinnetCallback cb = {ctx, &this_val, &opt};                                                                                                                                               \
    cb_ptr = cb;                                                                                                                                                                                       \
  }

#endif /* MINNET_WEBSOCKET_H */
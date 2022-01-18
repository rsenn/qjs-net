#ifndef MINNET_CLIENT_H
#define MINNET_CLIENT_H

#include <quickjs.h>
#include "minnet.h"
#include "buffer.h"

typedef struct client_data {
  JSContext* ctx;
  JSValue ws_obj;
  struct lws_context* lws;
  struct lws_context_creation_info info;
  MinnetURL url;
  struct byte_buffer body;
  BOOL connected : 1, closed : 1, h2 : 1;
  union {
    struct {
      MinnetCallback cb_message, cb_connect, cb_error, cb_close, cb_pong, cb_fd;
    };
    MinnetCallback callbacks[6];
  };
} MinnetClient;

extern THREAD_LOCAL JSClassID minnet_client_class_id;
extern THREAD_LOCAL JSValue minnet_client_proto, minnet_client_ctor;
extern JSClassDef minnet_client_class;
extern const JSCFunctionListEntry minnet_client_proto_funcs[];
extern const size_t minnet_client_proto_funcs_size;

JSValue minnet_ws_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue minnet_client_wrap(JSContext*, MinnetClient*);

static inline MinnetClient*
minnet_client_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_client_class_id);
}

static inline MinnetClient*
minnet_client_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_client_class_id);
}

#endif

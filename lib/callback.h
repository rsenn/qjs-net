#ifndef MINNET_CALLBACK_H
#define MINNET_CALLBACK_H

#include <quickjs.h>
#include <libwebsockets.h>

#define GETCBPROP(obj, opt, cb_ptr) GETCB(JS_GetPropertyStr(ctx, obj, opt), cb_ptr)
#define GETCB(opt, cb_ptr) GETCBTHIS(opt, cb_ptr, this_val)
#define GETCBTHIS(opt, cb_ptr, this_obj) \
  if(JS_IsFunction(ctx, opt)) { \
    cb_ptr = (MinnetCallback){ctx, JS_DupValue(ctx, this_obj), opt, #cb_ptr}; \
  }

#define FREECB(cb_ptr) \
  do { \
    JS_FreeValue(ctx, cb_ptr.this_obj); \
    JS_FreeValue(ctx, cb_ptr.func_obj); \
  } while(0);

#define FREECB_RT(cb_ptr) \
  do { \
    JS_FreeValueRT(rt, cb_ptr.this_obj); \
    JS_FreeValueRT(rt, cb_ptr.func_obj); \
  } while(0);

typedef struct ws_callback {
  JSContext* ctx;
  JSValue this_obj;
  JSValue func_obj;
  const char* name;
} MinnetCallback;

static inline void
callback_zero(MinnetCallback* cb) {
  cb->ctx = 0;
  cb->this_obj = JS_UNDEFINED;
  cb->func_obj = JS_NULL;
  cb->name = 0;
}

typedef struct callbacks {
  MinnetCallback message, connect, close, pong, fd, http, read, post;
} MinnetCallbacks;

static inline void
callbacks_zero(MinnetCallbacks* cbs) {
  callback_zero(&cbs->message);
  callback_zero(&cbs->connect);
  callback_zero(&cbs->close);
  callback_zero(&cbs->pong);
  callback_zero(&cbs->fd);
  callback_zero(&cbs->http);
}

int fd_handler(struct lws*, MinnetCallback*, struct lws_pollargs);
int fd_callback(struct lws*, enum lws_callback_reasons, MinnetCallback*, struct lws_pollargs* args);
JSValue minnet_emit_this(const struct ws_callback*, JSValue, int, JSValue* argv);
JSValue minnet_emit(const struct ws_callback*, int, JSValue*);

#endif /* MINNET_CALLBACK_H */

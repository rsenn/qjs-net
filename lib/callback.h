#ifndef QUICKJS_NET_LIB_CALLBACK_H
#define QUICKJS_NET_LIB_CALLBACK_H

#include <quickjs.h>
#include <libwebsockets.h>

#define GETCBPROP(obj, opt, cb_ptr) GETCB(JS_GetPropertyStr(ctx, obj, opt), cb_ptr)
#define GETCB(opt, cb_ptr) GETCBTHIS(opt, cb_ptr, this_val)
#define GETCBTHIS(opt, cb_ptr, this_obj) \
  if(JS_IsFunction(ctx, opt)) { \
    cb_ptr = (JSCallback){ctx, JS_DupValue(ctx, this_obj), opt, #cb_ptr}; \
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

typedef struct js_callback {
  JSContext* ctx;
  JSValue this_obj;
  JSValue func_obj;
  const char* name;
} JSCallback;

static inline void
callback_zero(JSCallback* cb) {
  cb->ctx = 0;
  cb->this_obj = JS_UNDEFINED;
  cb->func_obj = JS_NULL;
  cb->name = 0;
}

typedef enum callback_e { MESSAGE = 0, CONNECT, CLOSE, PONG, FD, HTTP, READ, POST, NUM_CALLBACKS } CallbackType;

typedef struct callbacks {
  union {
    struct {
      JSCallback message, connect, close, pong, fd, http, read, post;
    };
    JSCallback on[NUM_CALLBACKS];
  };
} CallbackList;

static inline void
callbacks_zero(CallbackList* cbs) {
  callback_zero(&cbs->message);
  callback_zero(&cbs->connect);
  callback_zero(&cbs->close);
  callback_zero(&cbs->pong);
  callback_zero(&cbs->fd);
  callback_zero(&cbs->http);
}

JSValue callback_emit_this(const struct js_callback*, JSValue, int, JSValue* argv);
JSValue callback_emit(const struct js_callback*, int, JSValue*);

#endif /* QUICKJS_NET_LIB_CALLBACK_H */

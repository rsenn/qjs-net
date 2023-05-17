#ifndef QJSNET_LIB_CALLBACK_H
#define QJSNET_LIB_CALLBACK_H

#include <quickjs.h>
#include <libwebsockets.h>

#define GETCBPROP(obj, opt, cb_ptr) GETCB(JS_GetPropertyStr(ctx, obj, opt), cb_ptr)
#define GETCB(opt, cb_ptr) GETCBTHIS(opt, cb_ptr, this_val)
#define GETCBTHIS(opt, cb_ptr, this_obj) \
  if(JS_IsFunction(ctx, opt)) { \
    cb_ptr = (JSCallback){opt, JS_DupValue(ctx, this_obj), ctx, #cb_ptr}; \
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

#define CALLBACK(ctx, func_obj, this_obj) \
  (JSCallback) { (func_obj), (this_obj), (ctx), 0 }

typedef struct js_callback {
  JSValue func_obj, this_obj;
  JSContext* ctx;
  const char* name;
} JSCallback;

static inline void
callback_zero(JSCallback* cb) {
  cb->ctx = 0;
  cb->this_obj = JS_UNDEFINED;
  cb->func_obj = JS_NULL;
  cb->name = 0;
}

static inline void
callback_clear(JSCallback* cb) {
  if(cb->ctx) {
    JS_FreeValue(cb->ctx, cb->this_obj);
    cb->this_obj = JS_UNDEFINED;
    JS_FreeValue(cb->ctx, cb->func_obj);
    cb->func_obj = JS_NULL;
  }
  cb->ctx = 0;
}

typedef enum callback_e { MESSAGE = 0, CONNECT, CLOSE, PONG, FD, HTTP, READ, POST, WRITEABLE, NUM_CALLBACKS } CallbackType;

typedef struct callbacks {
  union {
    struct {
      JSCallback message, connect, close, pong, fd, http, read, post, writeable;
    };
    JSCallback cb[NUM_CALLBACKS];
  };
} CallbackList;

static inline void
callbacks_zero(CallbackList* cbs) {
  for(int i = 0; i < NUM_CALLBACKS; i++) callback_zero(&cbs->cb[i]);
}

static inline void
callbacks_clear(CallbackList* cbs) {
  for(int i = 0; i < NUM_CALLBACKS; i++) callback_clear(&cbs->cb[i]);
}

static inline int
callback_valid(JSCallback const* cb) {
  return cb->ctx != 0 && JS_IsObject(cb->func_obj);
}

JSValue callback_emit_this(const struct js_callback*, JSValue, int, JSValue* argv);
JSValue callback_emit(const struct js_callback*, int, JSValue*);

#endif /* QJSNET_LIB_CALLBACK_H */

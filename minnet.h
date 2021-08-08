#ifndef MINNET_H
#define MINNET_H

#include <cutils.h>
#include <quickjs.h>
#include <libwebsockets.h>

struct http_request;

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

#define countof(x) (sizeof(x) / sizeof((x)[0]))
#define JS_CGETSET_MAGIC_FLAGS_DEF(prop_name, fgetter, fsetter, magic_num, flags)                                                                                                                      \
  {                                                                                                                                                                                                    \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET_MAGIC, .magic = magic_num, .u = {.getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} }               \
  }
#define JS_CGETSET_FLAGS_DEF(prop_name, fgetter, fsetter, flags)                                                                                                                                       \
  {                                                                                                                                                                                                    \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET, .u = {.getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} }                                         \
  }

#define SETLOG lws_set_log_level(LLL_ERR, NULL);

#define GETCB(opt, cb_ptr) GETCBTHIS(opt, cb_ptr, this_val)
#define GETCBTHIS(opt, cb_ptr, this_obj)                                                                                                                                                               \
  if(JS_IsFunction(ctx, opt)) {                                                                                                                                                                        \
    cb_ptr = (MinnetCallback){ctx, JS_DupValue(ctx, this_obj), JS_DupValue(ctx, opt), #cb_ptr};                                                                                                        \
  }

#define FREECB(cb_ptr)                                                                                                                                                                                 \
  do {                                                                                                                                                                                                 \
    JS_FreeValue(ctx, cb_ptr.this_obj);                                                                                                                                                                \
    JS_FreeValue(ctx, cb_ptr.func_obj);                                                                                                                                                                \
  } while(0);

#define ADD(ptr, inst, member)                                                                                                                                                                         \
  do {                                                                                                                                                                                                 \
    (*(ptr)) = (inst);                                                                                                                                                                                 \
    (ptr) = &(*(ptr))->member;                                                                                                                                                                         \
  } while(0);

enum { READ_HANDLER = 0, WRITE_HANDLER };
enum http_method;

typedef struct lws_pollfd MinnetPollFd;

typedef struct callback_ws {
  JSContext* ctx;
  JSValue this_obj;
  JSValue func_obj;
  const char* name;
} MinnetCallback;

extern JSContext* minnet_log_ctx;
extern BOOL minnet_exception;

extern JSClassID minnet_request_class_id;

int minnet_lws_unhandled(const char* handler, int);
JSValue minnet_emit_this(const struct callback_ws*, JSValueConst this_obj, int argc, JSValue* argv);
JSValue minnet_emit(const struct callback_ws*, int argc, JSValue* argv);
void minnet_handlers(JSContext*, struct lws* wsi, struct lws_pollargs* args, JSValue out[2]);
void value_dump(JSContext*, const char* n, JSValue const* v);
JSModuleDef* js_init_module_minnet(JSContext*, const char* module_name);
const char* lws_callback_name(int);

static inline size_t
byte_chr(const void* x, size_t len, char c) {
  const char *s, *t, *str = x;
  for(s = str, t = s + len; s < t; ++s)
    if(*s == c)
      break;
  return s - str;
}

static inline char*
lws_get_uri(struct lws* wsi, JSContext* ctx, enum lws_token_indexes token) {
  size_t len;
  char buf[1024];

  if((len = lws_hdr_copy(wsi, buf, sizeof(buf) - 1, token)) > 0)
    buf[len] = '\0';
  else
    return 0;

  return js_strndup(ctx, buf, len);
}

#endif /* MINNET_H */

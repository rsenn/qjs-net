#ifndef MINNET_H
#define MINNET_H

#include <cutils.h>
#include <quickjs.h>
#include <libwebsockets.h>

struct byte_buffer;
struct http_request;

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

#define countof(x) (sizeof(x) / sizeof((x)[0]))
#define JS_CGETSET_MAGIC_FLAGS_DEF(prop_name, fgetter, fsetter, magic_num, flags) \
  { \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET_MAGIC, .magic = magic_num, .u = {.getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} } \
  }
#define JS_CGETSET_FLAGS_DEF(prop_name, fgetter, fsetter, flags) \
  { \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET, .u = {.getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} } \
  }

#define SETLOG(max_level) lws_set_log_level(((((max_level) << 1) - 1) & (~LLL_PARSER)) | LLL_USER, NULL);

#define GETCB(opt, cb_ptr) GETCBTHIS(opt, cb_ptr, this_val)
#define GETCBTHIS(opt, cb_ptr, this_obj) \
  if(JS_IsFunction(ctx, opt)) { \
    cb_ptr = (MinnetCallback){ctx, JS_DupValue(ctx, this_obj), JS_DupValue(ctx, opt), #cb_ptr}; \
  }

#define FREECB(cb_ptr) \
  do { \
    JS_FreeValue(ctx, cb_ptr.this_obj); \
    JS_FreeValue(ctx, cb_ptr.func_obj); \
  } while(0);

#define ADD(ptr, inst, member) \
  do { \
    (*(ptr)) = (inst); \
    (ptr) = &(*(ptr))->member; \
  } while(0);

#define FG(c) "\x1b[38;5;" c "m"
#define BG(c) "\x1b[48;5;" c "m"
#define FGC(c, str) FG(#c) str NC
#define BGC(c, str) BG(#c) str NC
#define NC "\x1b[0m"

#ifdef _Thread_local
#define THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__SUNPRO_CC) || defined(__IBMCPP__)
#define THREAD_LOCAL __thread
#elif defined(_WIN32)
#define THREAD_LOCAL __declspec(thread)
#else
#error No TLS implementation found.
#endif

enum { READ_HANDLER = 0, WRITE_HANDLER };
enum http_method;

typedef struct lws_pollfd MinnetPollFd;

typedef struct ws_callback {
  JSContext* ctx;
  JSValue this_obj;
  JSValue func_obj;
  const char* name;
} MinnetCallback;

typedef struct url {
  char* protocol;
  char* host;
  int port;
  char* location;
} MinnetURL;

extern JSContext* minnet_log_ctx;
extern BOOL minnet_exception;

MinnetURL url_init(JSContext*, const char* proto, const char* host, uint16_t port, const char* path);
MinnetURL url_parse(JSContext* ctx, const char* url);
void url_free(JSContext*, MinnetURL* url);
JSValue header_object(JSContext*, const struct byte_buffer*);
int minnet_lws_unhandled(const char* handler, int);
JSValue minnet_emit_this(const struct ws_callback*, JSValueConst this_obj, int argc, JSValue* argv);
JSValue minnet_emit(const struct ws_callback*, int argc, JSValue* argv);
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

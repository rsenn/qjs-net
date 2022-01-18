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

#define MINNET_BUFFER_SIZE 1024

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

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

#define GETOPT(obj, name) JSValue opt_##name = JS_GetPropertyStr(ctx, (obj), #name);
#define FREEOPT(name) JS_FreeValue(ctx, opt_##name);

#define CB(obj, name, cb) ((cb).func_obj = JS_GetPropertyStr(ctx, (obj), (name)))
#define OPTIONS_CB(obj, n, cb) (((cb).ctx = ctx), ((cb).this_obj = JS_UNDEFINED), CB(obj,n,cb), ((cb).name = (n)))
//#define OPTIONS_CB(obj, name, cb) (cb) = (MinnetCallback){ctx, JS_UNDEFINED, JS_GetPropertyStr(ctx, (obj), (name)), (name)};
//#define GETCBPROP(obj, name, cb_ptr) GETCB(JS_GetPropertyStr(ctx, obj, name), cb_ptr)

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

#define MINNET_CALLBACK(name) \
  (MinnetCallback) { ctx, JS_UNDEFINED, JS_NULL, #name }

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

extern THREAD_LOCAL struct lws_context* minnet_lws_context;
extern JSContext* minnet_log_ctx;
extern BOOL minnet_exception;

MinnetURL url_init(JSContext*, const char* proto, const char* host, uint16_t port, const char* path);
MinnetURL url_parse(JSContext* ctx, const char* url);
void url_free(JSContext*, MinnetURL* url);
JSValue header_object(JSContext*, const struct byte_buffer*);
ssize_t header_set(JSContext*, struct byte_buffer*, const char* name, const char* value);
int fd_callback(struct lws*, enum lws_callback_reasons reason, MinnetCallback* cb, struct lws_pollargs* args);
int minnet_lws_unhandled(const char* handler, int);
JSValue minnet_emit_this(const struct ws_callback*, JSValueConst this_obj, int argc, JSValue* argv);
JSValue minnet_emit(const struct ws_callback*, int argc, JSValue* argv);
void minnet_handlers(JSContext*, struct lws* wsi, struct lws_pollargs* args, JSValue out[2]);
void value_dump(JSContext*, const char* n, JSValue const* v);
JSModuleDef* js_init_module_minnet(JSContext*, const char* module_name);
const char* lws_callback_name(int);

static inline BOOL
url_is_tls(MinnetURL* url) {
  if(url->protocol)
    return !strcmp(url->protocol, "wss") || !strcmp(url->protocol, "https");
  return FALSE;
}

static inline BOOL
url_is_raw(MinnetURL* url) {
  if(url->protocol)
    return strncmp(url->protocol, "ws", 2) && strncmp(url->protocol, "http", 4);
  return FALSE;
}

static inline size_t
byte_chr(const void* x, size_t len, char c) {
  const char *s, *t, *str = x;
  for(s = str, t = s + len; s < t; ++s)
    if(*s == c)
      break;
  return s - str;
}

static inline size_t
byte_chrs(const void* str, size_t len, const char needle[], size_t nl) {
  const char *s, *t;
  for(s = str, t = str + len; s != t; s++)
    if(byte_chr(needle, nl, *s) < nl)
      break;
  return s - (const char*)str;
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

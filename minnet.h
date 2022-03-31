#ifndef MINNET_H
#define MINNET_H

#include <cutils.h>
#include <quickjs.h>
#include <libwebsockets.h>
#include "jsutils.h"

union byte_buffer;
struct http_request;

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

#if defined(_WIN32) || defined(__MINGW32__)
#define VISIBLE __declspec(dllexport)
#define HIDDEN
#else
#define VISIBLE __attribute__((visibility("default")))
#define HIDDEN __attribute__((visibility("hidden")))
#endif

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

#define JS_INDEX_STRING_DEF(index, cstr) \
  { \
    .name = #index, .prop_flags = JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE, .def_type = JS_DEF_PROP_STRING, .magic = 0, .u = {.str = cstr } \
  }
#define JS_CFUNC_FLAGS_DEF(prop_name, length, func1, flags) \
  { \
    .name = prop_name, .prop_flags = (flags), .def_type = JS_DEF_CFUNC, .magic = 0, .u = {.func = {length, JS_CFUNC_generic, {.generic = func1}} } \
  }
#define SETLOG(max_level) lws_set_log_level(((((max_level) << 1) - 1) & (~LLL_PARSER)) | LLL_USER, NULL);

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

#define LOG(name, fmt, args...) lwsl_user("%-5s" FG("%d") "%-38s" NC " wsi#%" PRId64 " " fmt "\n", (name), 22 + (reason * 2), lws_callback_name(reason) + 13, opaque ? opaque->serial : -1, args);

#ifdef _Thread_local
#define THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__SUNPRO_CC) || defined(__IBMCPP__)
#define THREAD_LOCAL __thread
#elif defined(_WIN32)
#define THREAD_LOCAL __declspec(thread)
#else
#error No TLS implementation found.
#endif

#include "minnet-buffer.h"
#include "minnet-url.h"

enum { READ_HANDLER = 0, WRITE_HANDLER };
enum http_method;

typedef struct lws_pollfd MinnetPollFd;

typedef enum client_state {
  CONNECTING = 0,
  OPEN = 1,
  CLOSING = 2,
  CLOSED = 3,
} MinnetStatus;

typedef enum on_promise {
  ON_RESOLVE = 0,
  ON_REJECT,
} MinnetPromiseEvent;

typedef struct closure {
  int ref_count;
  union {
    struct context* context;
    struct client_context* client;
    struct server_context* server;
  };
  void (*free_func)(/*void**/);
} MinnetClosure;

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

struct proxy_connection;
struct http_mount;
struct server_context;
struct client_context;

enum http_method { METHOD_GET = 0, METHOD_POST, METHOD_OPTIONS, METHOD_PUT, METHOD_PATCH, METHOD_DELETE, METHOD_CONNECT, METHOD_HEAD };

typedef enum http_method MinnetHttpMethod;

typedef struct session_data {
  JSValue ws_obj;
  union {
    struct {
      JSValue req_obj;
      JSValue resp_obj;
    };
    JSValue args[2];
  };
  struct http_mount* mount;
  struct proxy_connection* proxy;
  JSValue generator, next;
  int serial;
  BOOL h2;
  int64_t written;
  struct server_context* server;
  struct client_context* client;
  MinnetBuffer send_buf;
  MinnetURL url;
  MinnetHttpMethod method;
} MinnetSession;

typedef struct callbacks {
  MinnetCallback message, connect, close, pong, fd, http;
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

typedef struct context {
  int ref_count;
  JSContext* js;
  struct lws_context* lws;
  struct lws_context_creation_info info;
  BOOL exception;
  JSValue error;
  JSValue crt, key, ca;
} MinnetContext;

extern THREAD_LOCAL int32_t minnet_log_level;
extern THREAD_LOCAL JSContext* minnet_log_ctx;
extern THREAD_LOCAL BOOL minnet_exception;

int socket_geterror(int);
void session_zero(MinnetSession*);
void session_clear(MinnetSession*, JSContext*);
BOOL context_exception(MinnetContext*, JSValue);
void context_clear(MinnetContext*);
MinnetClosure* closure_new(JSContext*);
MinnetClosure* closure_dup(MinnetClosure*);
void closure_free(void*);
int minnet_lws_unhandled(const char*, int);
JSValue headers_object(JSContext*, const void*, const void*);
char* headers_atom(JSAtom, JSContext*);
int headers_add(MinnetBuffer*, struct lws*, JSValue, JSContext* ctx);
int headers_fromobj(MinnetBuffer*, JSValue, JSContext*);
ssize_t headers_set(JSContext*, MinnetBuffer*, const char*, const char* value);
int headers_get(JSContext*, MinnetBuffer*, struct lws*);
int fd_handler(struct lws*, MinnetCallback*, struct lws_pollargs);
int fd_callback(struct lws*, enum lws_callback_reasons, MinnetCallback*, struct lws_pollargs* args);
void minnet_handlers(JSContext*, struct lws*, struct lws_pollargs, JSValue out[2]);
JSValue minnet_emit_this(const struct ws_callback*, JSValue, int, JSValue* argv);
JSValue minnet_emit(const struct ws_callback*, int, JSValue*);
void value_dump(JSContext*, const char*, JSValue const*);
JSModuleDef* js_init_module_minnet(JSContext*, const char*);
const char* lws_callback_name(int);

static inline BOOL
lws_is_poll_callback(int reason) {
  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL:
    case LWS_CALLBACK_ADD_POLL_FD:
    case LWS_CALLBACK_DEL_POLL_FD:
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: return TRUE;
  }
  return FALSE;
}

static inline BOOL
lws_is_http_callback(int reason) {
  switch(reason) {
    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
    case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP:
    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
    case LWS_CALLBACK_CLIENT_HTTP_REDIRECT:
    case LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_CLIENT_HTTP_DROP_PROTOCOL:
    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:

    case LWS_CALLBACK_ADD_HEADERS:
    case LWS_CALLBACK_CLOSED_HTTP:
    case LWS_CALLBACK_FILTER_HTTP_CONNECTION:
    case LWS_CALLBACK_HTTP:
    case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_HTTP_BODY:
    case LWS_CALLBACK_HTTP_BODY_COMPLETION:
    case LWS_CALLBACK_HTTP_DROP_PROTOCOL:
    case LWS_CALLBACK_HTTP_FILE_COMPLETION:
    case LWS_CALLBACK_HTTP_WRITEABLE: return TRUE;
  }
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

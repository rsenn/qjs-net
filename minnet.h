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

#define LOG(name, fmt, args...) \
  lwsl_user("%-5s" \
            " " fmt "\n", \
            (char*)(name), \
            args);
#define LOGCB(name, fmt, args...) LOG((name), FG("%d") "%-38s" NC " wsi#%" PRId64 " " fmt "", 22 + (reason * 2), lws_callback_name(reason) + 13, opaque ? opaque->serial : -1, args);

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

struct proxy_connection;
struct http_mount;
struct server_context;
struct client_context;

enum http_method {
  METHOD_GET = 0,
  METHOD_POST,
  METHOD_OPTIONS,
  METHOD_PATCH,
  METHOD_PUT,
  METHOD_DELETE,
  METHOD_HEAD,
};

typedef enum http_method MinnetHttpMethod;
 
 
typedef struct context {
  int ref_count;
  JSContext* js;
  struct lws_context* lws;
  struct lws_context_creation_info info;
  BOOL exception;
  JSValue error;
  JSValue crt, key, ca;
  struct TimerClosure* timer;
} MinnetContext;

extern THREAD_LOCAL int32_t minnet_log_level;
extern THREAD_LOCAL JSContext* minnet_log_ctx;
extern THREAD_LOCAL BOOL minnet_exception;
extern THREAD_LOCAL struct list_head minnet_sockets;

int socket_geterror(int);
JSValue context_exception(MinnetContext*, JSValue);
void context_clear(MinnetContext*);
MinnetClosure* closure_new(JSContext*);
MinnetClosure* closure_dup(MinnetClosure*);
void closure_free(void*);
int minnet_lws_unhandled(const char*, int);
JSValue headers_object(JSContext*, const void*, const void*);
char* headers_atom(JSAtom, JSContext*);
int headers_addobj(MinnetBuffer*, struct lws*, JSValueConst, JSContext* ctx);
size_t headers_write(uint8_t**, uint8_t*, MinnetBuffer*, struct lws* wsi);
int headers_fromobj(MinnetBuffer*, JSValueConst, JSContext*);
ssize_t headers_set(JSContext*, MinnetBuffer*, const char*, const char* value);
ssize_t headers_findb(MinnetBuffer*, const char*, size_t);
ssize_t headers_find(MinnetBuffer*, const char*);
char* headers_at(MinnetBuffer* buffer, size_t* lenptr, size_t index);
char* headers_get(MinnetBuffer*, size_t*, const char*);
ssize_t headers_copy(MinnetBuffer*, char*, size_t, const char* name);
ssize_t headers_unsetb(MinnetBuffer*, const char*, size_t);
ssize_t headers_unset(MinnetBuffer*, const char*);
int headers_tostring(JSContext*, MinnetBuffer*, struct lws*);
void minnet_handlers(JSContext*, struct lws*, struct lws_pollargs, JSValue out[2]);
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

static inline BOOL
lws_is_client_callback(int reason) {
  switch(reason) {
    case LWS_CALLBACK_CONNECTING:
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
    case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED:
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
    case LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_CLIENT_HTTP_DROP_PROTOCOL:
    case LWS_CALLBACK_CLIENT_HTTP_REDIRECT:
    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
    case LWS_CALLBACK_CLIENT_RECEIVE:
    case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
    case LWS_CALLBACK_CLIENT_WRITEABLE:
    case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
    case LWS_CALLBACK_MQTT_CLIENT_CLOSED:
    case LWS_CALLBACK_MQTT_CLIENT_ESTABLISHED:
    case LWS_CALLBACK_MQTT_CLIENT_RX:
    case LWS_CALLBACK_MQTT_CLIENT_WRITEABLE:
    case LWS_CALLBACK_MQTT_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
    case LWS_CALLBACK_OPENSSL_PERFORM_CLIENT_CERT_VERIFICATION:
    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP:
    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_WS_CLIENT_BIND_PROTOCOL:
    case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL: return TRUE;
  }
  return FALSE;
}

char* lws_get_peer(struct lws* wsi, JSContext* ctx);
char* fd_address(int, int (*fn)(int, struct sockaddr*, socklen_t*));
char* fd_remote(int fd);
char* fd_local(int fd);

int lws_wsi_is_h2(struct lws* wsi);

static inline int
is_h2(struct lws* wsi) {
  return lws_wsi_is_h2(wsi);
}

static inline int
minnet_query_length(struct lws* wsi) {
  return lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
}
char* lws_get_token_len(struct lws*, JSContext*, enum lws_token_indexes, size_t* len_p);
char* lws_get_token(struct lws*, JSContext*, enum lws_token_indexes);
int lws_copy_fragment(struct lws*, enum lws_token_indexes, int, DynBuf* db);
int minnet_query_object2(struct lws*, JSContext*, JSValueConst);
int minnet_query_object(struct lws*, JSContext*, JSValueConst);

char* lws_get_host(struct lws* wsi, JSContext* ctx);
void lws_peer_cert(struct lws*);

static inline char*
minnet_uri_and_method(struct lws* wsi, JSContext* ctx, MinnetHttpMethod* method) {
  char* url;

  if((url = lws_get_token(wsi, ctx, WSI_TOKEN_POST_URI))) {
    if(method)
      *method = METHOD_POST;
  } else if((url = lws_get_token(wsi, ctx, WSI_TOKEN_GET_URI))) {
    if(method)
      *method = METHOD_GET;
  } else if((url = lws_get_token(wsi, ctx, WSI_TOKEN_HEAD_URI))) {
    if(method)
      *method = METHOD_HEAD;
  } else if((url = lws_get_token(wsi, ctx, WSI_TOKEN_OPTIONS_URI))) {
    if(method)
      *method = METHOD_OPTIONS;
  } else if((url = lws_get_token(wsi, ctx, WSI_TOKEN_PATCH_URI))) {
    if(method)
      *method = METHOD_PATCH;
  } else if((url = lws_get_token(wsi, ctx, WSI_TOKEN_PUT_URI))) {
    if(method)
      *method = METHOD_PUT;
  }

  return url;
}

#endif /* MINNET_H */

#ifndef QJSNET_LIB_UTILS_H
#define QJSNET_LIB_UTILS_H

#include <stddef.h>
#include <ctype.h>
#include <libwebsockets.h>
#include <arpa/inet.h>
#include <quickjs.h>
#include <cutils.h>

#if defined(_WIN32) || defined(__MINGW32__)
#define VISIBLE __declspec(dllexport)
#define HIDDEN
#else
#define VISIBLE __attribute__((visibility("default")))
#define HIDDEN __attribute__((visibility("hidden")))
#endif

#ifdef _Thread_local
#define THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__SUNPRO_CC) || defined(__IBMCPP__)
#define THREAD_LOCAL __thread
#elif defined(_WIN32)
#define THREAD_LOCAL __declspec(thread)
#else
#error No TLS implementation found.
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define FG(c) "\x1b[38;5;" c "m"
#define BG(c) "\x1b[48;5;" c "m"
#define FGC(c, str) FG(#c) str NC
#define BGC(c, str) BG(#c) str NC
#define NC "\x1b[0m"

enum http_method {
  METHOD_GET = 0,
  METHOD_POST,
  METHOD_OPTIONS,
  METHOD_PATCH,
  METHOD_PUT,
  METHOD_DELETE,
  METHOD_HEAD,
};

typedef enum http_method HTTPMethod;

#define countof(x) (sizeof(x) / sizeof((x)[0]))

size_t str_chr(const char*, char);
size_t byte_chr(const void*, size_t, char);
size_t byte_chrs(const void*, size_t, const char[], size_t);
size_t byte_rchr(const void*, size_t, char);
size_t scan_whitenskip(const void*, size_t);
size_t scan_nonwhitenskip(const void*, size_t);
size_t scan_eol(const void*, size_t);
size_t scan_nextline(const void*, size_t);
size_t scan_charsetnskip(const void*, const char*, size_t);
size_t scan_noncharsetnskip(const void*, const char*, size_t);

size_t skip_brackets(const char*, size_t len);
size_t skip_directory(const char*, size_t len);
size_t strip_trailing_newline(const char*, size_t* len_p);

unsigned uint_pow(unsigned, unsigned degree);

int socket_geterror(int);
char* socket_address(int, int (*fn)(int, struct sockaddr*, socklen_t*));

BOOL wsi_http2(struct lws*);
BOOL wsi_tls(struct lws*);
char* wsi_peer(struct lws*, JSContext* ctx);
char* wsi_host(struct lws*, JSContext* ctx);
void wsi_cert(struct lws*);
char* wsi_query_string_len(struct lws*, size_t* len_p, JSContext* ctx);
// int wsi_query_object(struct lws*, JSContext* ctx, JSValueConst obj);

BOOL wsi_token_exists(struct lws* wsi, enum lws_token_indexes token);
char* wsi_token_len(struct lws*, JSContext* ctx, enum lws_token_indexes token, size_t* len_p);
int wsi_copy_fragment(struct lws*, enum lws_token_indexes token, int fragment, DynBuf* db);
char* wsi_uri_and_method(struct lws*, JSContext* ctx, HTTPMethod* method);
HTTPMethod wsi_method(struct lws* wsi);
enum lws_token_indexes wsi_uri_token(struct lws* wsi);
char* wsi_host_and_port(struct lws* wsi, JSContext* ctx, int* port);
char* wsi_vhost_and_port(struct lws* wsi, JSContext* ctx, int* port);
const char* wsi_vhost_name(struct lws* wsi);
const char* wsi_protocol_name(struct lws* wsi);

const char* lws_callback_name(int reason);

static inline BOOL
has_query(const char* str) {
  return !!strchr(str, '?');
}

static inline BOOL
has_query_b(const char* str, size_t len) {
  return byte_chr(str, len, '?') < len;
}

static inline char*
wsi_token(struct lws* wsi, JSContext* ctx, enum lws_token_indexes token) {
  return wsi_token_len(wsi, ctx, token, NULL);
}

static inline BOOL
lws_reason_poll(int reason) {
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
lws_reason_http(int reason) {
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
lws_reason_client(int reason) {
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

static inline int
wsi_query_len(struct lws* wsi) {
  return lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
}

static inline char*
socket_remote(int fd) {
  return socket_address(fd, &getpeername);
}

static inline char*
socket_local(int fd) {
  return socket_address(fd, &getsockname);
}
#endif /* QJSNET_LIB_UTILS_H */

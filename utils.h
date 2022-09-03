#ifndef MINNET_UTILS_H
#define MINNET_UTILS_H

#include <stddef.h>
#include <ctype.h>
#include <libwebsockets.h>

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

#define countof(x) (sizeof(x) / sizeof((x)[0]))
static inline size_t
byte_chr(const void* x, size_t len, char c) {
  const char *s, *t, *str = x;
  for(s = str, t = s + len; s < t; ++s)
    if(*s == c)
      break;
  return s - str;
}

static inline size_t
byte_chrs(const void* x, size_t len, const char needle[], size_t nl) {
  const char *s, *t;
  for(s = x, t = (const char*)x + len; s != t; s++)
    if(byte_chr(needle, nl, *s) < nl)
      break;
  return s - (const char*)x;
}

static inline size_t
byte_rchr(const void* x, size_t len, char needle) {
  const char *s, *t;
  for(s = x, t = (const char*)x + len; --t >= s;) {
    if(*t == needle)
      return (size_t)(t - s);
  }
  return len;
}

static inline size_t
scan_whitenskip(const char* s, size_t limit) {
  const char* t = s;
  const char* u = t + limit;
  while(t < u && isspace(*t)) ++t;
  return (size_t)(t - s);
}

static inline size_t
scan_nonwhitenskip(const char* s, size_t limit) {
  const char* t = s;
  const char* u = t + limit;
  while(t < u && !isspace(*t)) ++t;
  return (size_t)(t - s);
}

static inline size_t
scan_charsetnskip(const char* s, const char* charset, size_t limit) {
  const char* t = s;
  const char* u = t + limit;
  const char* i;
  while(t < u) {
    for(i = charset; *i; ++i)
      if(*i == *t)
        break;
    if(*i != *t)
      break;
    ++t;
  }
  return (size_t)(t - s);
}

static inline unsigned
uint_pow(unsigned base, unsigned degree) {
  unsigned result = 1;
  unsigned term = base;
  while(degree) {
    if(degree & 1)
      result *= term;
    term *= term;
    degree = degree >> 1;
  }
  return result;
}

static inline char*
lws_get_token_len(struct lws* wsi, JSContext* ctx, enum lws_token_indexes token, size_t* len_p) {
  size_t len;
  int r;
  char* buf;

  len = lws_hdr_total_length(wsi, token);

  if(!(buf = js_mallocz(ctx, len + 1)))
    return 0;

  lws_hdr_copy(wsi, buf, len + 1, token);

  if(len_p)
    *len_p = len;

  return buf;
}

static inline char*
lws_get_token(struct lws* wsi, JSContext* ctx, enum lws_token_indexes token) {
  return lws_get_token_len(wsi, ctx, token, NULL);
}

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

static inline int
is_h2(struct lws* wsi) {
  return lws_wsi_is_h2(wsi);
}

static inline int
minnet_query_length(struct lws* wsi) {
  return lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
}

#endif /* MINNET_UTILS_H */

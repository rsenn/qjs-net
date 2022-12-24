#ifndef QJSNET_LIB_LWS_UTILS_H
#define QJSNET_LIB_LWS_UTILS_H

#include <libwebsockets.h>
#include <cutils.h>
#include <stdbool.h>

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

static inline void*
wsi_context(struct lws* wsi) {
  return lws_context_user(lws_get_context(wsi));
}

bool wsi_http2(struct lws*);
bool wsi_tls(struct lws*);
char* wsi_peer(struct lws*);
char* wsi_host(struct lws*);
void wsi_cert(struct lws*);
char* wsi_query_string_len(struct lws*, size_t* len_p);
bool wsi_token_exists(struct lws*, enum lws_token_indexes token);
char* wsi_token_len(struct lws*, enum lws_token_indexes token, size_t* len_p);
int wsi_copy_fragment(struct lws*, enum lws_token_indexes token, int fragment, DynBuf* db);
char* wsi_uri_and_method(struct lws*, HTTPMethod* method);
char* wsi_host_and_port(struct lws*, int* port);
const char* wsi_vhost_name(struct lws*);
const char* wsi_protocol_name(struct lws*);
char* wsi_vhost_and_port(struct lws*, int* port);
enum lws_token_indexes wsi_uri_token(struct lws*);
HTTPMethod wsi_method(struct lws*);
char* wsi_ipaddr(struct lws*);
const char* lws_callback_name(int);

static inline char*
wsi_token(struct lws* wsi, enum lws_token_indexes token) {
  return wsi_token_len(wsi, token, NULL);
}

static inline bool
lws_reason_poll(int reason) {
  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL:
    case LWS_CALLBACK_ADD_POLL_FD:
    case LWS_CALLBACK_DEL_POLL_FD:
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: return true;
  }
  return false;
}

static inline bool
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
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
    case LWS_CALLBACK_ADD_HEADERS:
    case LWS_CALLBACK_CLOSED_HTTP:
    case LWS_CALLBACK_FILTER_HTTP_CONNECTION:
    case LWS_CALLBACK_HTTP:
    case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_HTTP_BODY:
    case LWS_CALLBACK_HTTP_BODY_COMPLETION:
    case LWS_CALLBACK_HTTP_DROP_PROTOCOL:
    case LWS_CALLBACK_HTTP_FILE_COMPLETION:
    case LWS_CALLBACK_HTTP_WRITEABLE: return true;
  }
  return false;
}

static inline bool
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
    case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL: return true;
  }
  return false;
}

static inline int
wsi_query_len(struct lws* wsi) {
  return lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
}

#endif /* QJSNET_LIB_LWS_UTILS_H */



#include "utils.h"

size_t
str_chr(const char* in, char needle) {
  const char* t;

  for(t = in; *t; ++t)
    if(*t == needle)
      break;

  return (size_t)(t - in);
}

size_t
byte_chr(const void* x, size_t len, char c) {
  const char *s, *t, *str = x;

  for(s = str, t = s + len; s < t; ++s)
    if(*s == c)
      break;

  return s - str;
}

size_t
byte_chrs(const void* x, size_t len, const char needle[], size_t nl) {
  const char *s, *t;

  for(s = x, t = (const char*)x + len; s != t; s++)
    if(byte_chr(needle, nl, *s) < nl)
      break;

  return s - (const char*)x;
}

size_t
byte_rchr(const void* x, size_t len, char needle) {
  const char *s, *t;

  for(s = x, t = (const char*)x + len; --t >= s;)
    if(*t == needle)
      return (size_t)(t - s);

  return len;
}

int
byte_diff(const void* a, size_t len, const void* b) {
  size_t i;
  for(i = 0; i < len; ++i) {
    int r = ((unsigned char*)a)[i] - ((unsigned char*)b)[i];
    if(r)
      return r;
  }
  return 0;
}

size_t
byte_equal(const void* s, size_t n, const void* t) {
  return byte_diff(s, n, t) == 0;
}

size_t
byte_findb(const void* haystack, size_t hlen, const void* what, size_t wlen) {
  size_t i, last;
  const char* s = (const char*)haystack;
  if(hlen < wlen)
    return hlen;
  last = hlen - wlen;
  for(i = 0; i <= last; i++) {
    if(byte_equal(s, wlen, what))
      return i;
    s++;
  }
  return hlen;
}

size_t
scan_whitenskip(const void* s, size_t limit) {
  const char *t = s, *u = t + limit;

  while(t < u && isspace(*t)) ++t;

  return t - (const char*)s;
}

size_t
scan_nonwhitenskip(const void* s, size_t limit) {
  const char *t = s, *u = t + limit;

  while(t < u && !isspace(*t)) ++t;

  return t - (const char*)s;
}

size_t
scan_eol(const void* s, size_t limit) {
  const char* t = s;
  size_t i = byte_chr(s, limit, '\n');

  while(i > 0 && t[i - 1] == '\r') i--;

  return i;
}

size_t
scan_nextline(const void* s, size_t limit) {
  size_t i;

  if((i = byte_chr(s, limit, '\n')) < limit)
    i++;

  return i;
}

size_t
scan_charsetnskip(const void* s, const char* charset, size_t limit) {
  const char *t, *u, *i;

  for(t = s, u = t + limit; t < u; t++) {
    for(i = charset; *i; i++)
      if(*i == *t)
        break;
    if(*i != *t)
      break;
  }
  return t - (const char*)s;
}

size_t
scan_noncharsetnskip(const void* s, const char* charset, size_t limit) {
  const char *t, *u, *i;

  for(t = s, u = t + limit; t < u; t++) {
    for(i = charset; *i; i++)
      if(*i == *t)
        break;
    if(*i == *t)
      break;
  }

  return t - (const char*)s;
}

size_t
skip_brackets(const char* line, size_t len) {
  size_t n = 0;

  if(len > 0 && line[0] == '[') {
    if((n = byte_chr(line, len, ']')) < len)
      n++;

    while(n < len && isspace(line[n])) n++;

    if(n + 1 < len && line[n + 1] == ':')
      n += 2;

    while(n < len && (isspace(line[n]) || line[n] == '-')) n++;
  }

  return n;
}

size_t
skip_directory(const char* line, size_t len) {
  if(line[0] == '/') {
    size_t colon = byte_chr(line, len, ':');
    size_t slash = byte_rchr(line, colon, '/');

    if(slash < colon)
      return slash + 1;
  }

  return 0;
}

size_t
strip_trailing_newline(const char* line, size_t* len_p) {
  size_t len = *len_p;

  while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;

  return *len_p = len;
}

unsigned
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

int
socket_geterror(int fd) {
  int e;
  socklen_t sl = sizeof(e);

  if(!getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &sl)) {
    setsockopt(fd, SOL_SOCKET, SO_ERROR, &e, sl);
    return e;
  }

  return -1;
}

char*
socket_address(int fd, int (*fn)(int, struct sockaddr*, socklen_t*)) {
  const char* s = 0;
  union {
    struct sockaddr a;
    struct sockaddr_in ai;
    struct sockaddr_in6 ai6;
  } sa;
  socklen_t sl = sizeof(s);
  uint16_t port;
  static char addr[1024];

  if(fn(fd, &sa.a, &sl) != -1) {
    size_t i;
    s = inet_ntop(sa.ai.sin_family, sa.ai.sin_family == AF_INET ? (void*)&sa.ai.sin_addr : (void*)&sa.ai6.sin6_addr, addr, sizeof(addr));
    i = strlen(s);

    switch(sa.ai.sin_family) {
      case AF_INET: port = ntohs(sa.ai.sin_port); break;
      case AF_INET6: port = ntohs(sa.ai6.sin6_port); break;
    }
    snprintf(&addr[i], sizeof(addr) - i, ":%u", port);
  }

  return (char*)s;
}

int lws_wsi_is_h2(struct lws* wsi);
int lws_is_ssl(struct lws* wsi);

BOOL
wsi_http2(struct lws* wsi) {
  return lws_wsi_is_h2(wsi);
}

BOOL
wsi_tls(struct lws* wsi) {
  return lws_is_ssl(lws_get_network_wsi(wsi));
}

char*
wsi_peer(struct lws* wsi, JSContext* ctx) {
  char buf[1024];

  lws_get_peer_simple(wsi, buf, sizeof(buf) - 1);

  return js_strdup(ctx, buf);
}

char*
wsi_host(struct lws* wsi, JSContext* ctx) {
  return wsi_token(wsi, ctx, lws_wsi_is_h2(wsi) ? WSI_TOKEN_HTTP_COLON_AUTHORITY : WSI_TOKEN_HOST);
}

void
wsi_cert(struct lws* wsi) {
  uint8_t buf[1280];
  union lws_tls_cert_info_results* ci = (union lws_tls_cert_info_results*)buf;
#if defined(LWS_HAVE_CTIME_R)
  char date[32];
#endif

  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_COMMON_NAME, ci, sizeof(buf) - sizeof(*ci)))
    lwsl_notice(" Peer Cert CN        : %s\n", ci->ns.name);

  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_ISSUER_NAME, ci, sizeof(ci->ns.name)))
    lwsl_notice(" Peer Cert issuer    : %s\n", ci->ns.name);

#if defined(LWS_HAVE_CTIME_R)
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_VALIDITY_FROM, ci, 0))
    lwsl_notice(" Peer Cert Valid from: %s", ctime_r(&ci->time, date));
#else
  lwsl_notice(" Peer Cert Valid from: %s", ctime(&ci->time));
#endif
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_VALIDITY_TO, ci, 0))
#if defined(LWS_HAVE_CTIME_R)
    lwsl_notice(" Peer Cert Valid to  : %s", ctime_r(&ci->time, date));
#else
    lwsl_notice(" Peer Cert Valid to  : %s", ctime(&ci->time));
#endif
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_USAGE, ci, 0))
    lwsl_notice(" Peer Cert usage bits: 0x%x\n", ci->usage);
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_OPAQUE_PUBLIC_KEY, ci, sizeof(buf) - sizeof(*ci))) {
    lwsl_notice(" Peer Cert public key:\n");
    lwsl_hexdump_notice(ci->ns.name, (unsigned int)ci->ns.len);
  }

  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_AUTHORITY_KEY_ID, ci, 0)) {
    lwsl_notice(" AUTHORITY_KEY_ID\n");
    lwsl_hexdump_notice(ci->ns.name, (size_t)ci->ns.len);
  }
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_AUTHORITY_KEY_ID_ISSUER, ci, 0)) {
    lwsl_notice(" AUTHORITY_KEY_ID ISSUER\n");
    lwsl_hexdump_notice(ci->ns.name, (size_t)ci->ns.len);
  }
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_AUTHORITY_KEY_ID_SERIAL, ci, 0)) {
    lwsl_notice(" AUTHORITY_KEY_ID SERIAL\n");
    lwsl_hexdump_notice(ci->ns.name, (size_t)ci->ns.len);
  }
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_SUBJECT_KEY_ID, ci, 0)) {
    lwsl_notice(" AUTHORITY_KEY_ID SUBJECT_KEY_ID\n");
    lwsl_hexdump_notice(ci->ns.name, (size_t)ci->ns.len);
  }
}

char*
wsi_query_string_len(struct lws* wsi, size_t* len_p, JSContext* ctx) {
  if(!wsi_token_exists(wsi, WSI_TOKEN_HTTP_URI_ARGS)) {
    if(len_p)
      *len_p = 0;
    return 0;
  }

  return wsi_token_len(wsi, ctx, WSI_TOKEN_HTTP_URI_ARGS, len_p);
}

BOOL
wsi_token_exists(struct lws* wsi, enum lws_token_indexes token) {
  return lws_hdr_total_length(wsi, token) > 0;
}

char*
wsi_token_len(struct lws* wsi, JSContext* ctx, enum lws_token_indexes token, size_t* len_p) {
  size_t len;
  char* buf;

  len = lws_hdr_total_length(wsi, token);

  if(!(buf = js_mallocz(ctx, len + 1)))
    return 0;

  lws_hdr_copy(wsi, buf, len + 1, token);

  if(len_p)
    *len_p = len;

  return buf;
}

int
wsi_copy_fragment(struct lws* wsi, enum lws_token_indexes token, int fragment, DynBuf* db) {
  int ret = 0, len;
  // dbuf_init2(&dbuf, 0, 0);

  len = lws_hdr_fragment_length(wsi, token, fragment);

  dbuf_realloc(db, (len > 0 ? len : 1023) + 1);

  if((ret = lws_hdr_copy_fragment(wsi, (void*)db->buf, db->size, token, fragment)) < 0)
    return ret;

  return len;
}

char*
wsi_uri_and_method(struct lws* wsi, JSContext* ctx, HTTPMethod* method) {
  char* url;

  if((url = wsi_token(wsi, ctx, WSI_TOKEN_POST_URI))) {
    if(method)
      *method = METHOD_POST;
  } else if((url = wsi_token(wsi, ctx, WSI_TOKEN_GET_URI))) {
    if(method)
      *method = METHOD_GET;
  } else if((url = wsi_token(wsi, ctx, WSI_TOKEN_HEAD_URI))) {
    if(method)
      *method = METHOD_HEAD;
  } else if((url = wsi_token(wsi, ctx, WSI_TOKEN_OPTIONS_URI))) {
    if(method)
      *method = METHOD_OPTIONS;
  } else if((url = wsi_token(wsi, ctx, WSI_TOKEN_PATCH_URI))) {
    if(method)
      *method = METHOD_PATCH;
  } else if((url = wsi_token(wsi, ctx, WSI_TOKEN_PUT_URI))) {
    if(method)
      *method = METHOD_PUT;
  }

  return url;
}

char*
wsi_host_and_port(struct lws* wsi, JSContext* ctx, int* port) {
  char* host;
  size_t hostlen;

  if((host = wsi_token_len(wsi, ctx, WSI_TOKEN_HOST, &hostlen))) {
    size_t pos;

    if((pos = byte_chr(host, hostlen, ':')) < hostlen) {
      *port = atoi(&host[pos + 1]);
      host[pos] = '\0';
      host = js_realloc(ctx, host, pos + 1);
    }
  }
  return host;
}

const char*
wsi_vhost_name(struct lws* wsi) {
  struct lws_vhost* vhost;

  if((vhost = lws_get_vhost(wsi)))
    return lws_get_vhost_name(vhost);

  return 0;
}

const char*
wsi_protocol_name(struct lws* wsi) {
  const struct lws_protocols* protocol;

  if((protocol = lws_get_protocol(wsi)))
    return protocol->name;

  return 0;
}

char*
wsi_vhost_and_port(struct lws* wsi, JSContext* ctx, int* port) {
  char* host = 0;
  struct lws_vhost* vhost;

  if((vhost = lws_get_vhost(wsi))) {
    const char* vhn = lws_get_vhost_name(vhost);
    size_t hostlen = str_chr(vhn, ':');
    host = js_strndup(ctx, vhn, hostlen);

    // printf("%s() host=%s port=%u\n", __func__, host, lws_get_vhost_port(vhost));

    if(port)
      *port = lws_get_vhost_port(vhost);
  }

  return host;
}

static const enum lws_token_indexes wsi_uri_tokens[] = {
    WSI_TOKEN_GET_URI,
    WSI_TOKEN_POST_URI,
    WSI_TOKEN_OPTIONS_URI,
    WSI_TOKEN_PATCH_URI,
    WSI_TOKEN_PUT_URI,
    WSI_TOKEN_DELETE_URI,
    WSI_TOKEN_HEAD_URI,
};

enum lws_token_indexes
wsi_uri_token(struct lws* wsi) {

  size_t i;

  for(i = 0; i < countof(wsi_uri_tokens); i++)
    if(wsi_token_exists(wsi, wsi_uri_tokens[i]))
      return wsi_uri_tokens[i];

  return -1;
}

HTTPMethod
wsi_method(struct lws* wsi) {
  static const HTTPMethod methods[] = {
      METHOD_GET,
      METHOD_POST,
      METHOD_OPTIONS,
      METHOD_PATCH,
      METHOD_PUT,
      METHOD_DELETE,
      METHOD_HEAD,
  };

  for(size_t i = 0; i < countof(wsi_uri_tokens); i++)
    if(wsi_token_exists(wsi, wsi_uri_tokens[i]))
      return methods[i];

  return -1;
}

char*
wsi_ipaddr(struct lws* wsi, JSContext* ctx) {
  char ipaddr[16], *ret = 0;

  if(lws_get_peer_simple(wsi, ipaddr, sizeof(ipaddr)))
    ret = js_strdup(ctx, ipaddr);

  return ret;
}

void
js_value_dump(JSContext* ctx, const char* n, JSValueConst const* v) {
  const char* str = JS_ToCString(ctx, *v);
  lwsl_user("%s = '%s'\n", n, str);
  JS_FreeCString(ctx, str);
}

const char*
lws_callback_name(int reason) {
  return ((const char* const[]){
      "LWS_CALLBACK_ESTABLISHED",
      "LWS_CALLBACK_CLIENT_CONNECTION_ERROR",
      "LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH",
      "LWS_CALLBACK_CLIENT_ESTABLISHED",
      "LWS_CALLBACK_CLOSED",
      "LWS_CALLBACK_CLOSED_HTTP",
      "LWS_CALLBACK_RECEIVE",
      "LWS_CALLBACK_RECEIVE_PONG",
      "LWS_CALLBACK_CLIENT_RECEIVE",
      "LWS_CALLBACK_CLIENT_RECEIVE_PONG",
      "LWS_CALLBACK_CLIENT_WRITEABLE",
      "LWS_CALLBACK_SERVER_WRITEABLE",
      "LWS_CALLBACK_HTTP",
      "LWS_CALLBACK_HTTP_BODY",
      "LWS_CALLBACK_HTTP_BODY_COMPLETION",
      "LWS_CALLBACK_HTTP_FILE_COMPLETION",
      "LWS_CALLBACK_HTTP_WRITEABLE",
      "LWS_CALLBACK_FILTER_NETWORK_CONNECTION",
      "LWS_CALLBACK_FILTER_HTTP_CONNECTION",
      "LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED",
      "LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION",
      "LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS",
      "LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS",
      "LWS_CALLBACK_OPENSSL_PERFORM_CLIENT_CERT_VERIFICATION",
      "LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER",
      "LWS_CALLBACK_CONFIRM_EXTENSION_OKAY",
      "LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED",
      "LWS_CALLBACK_PROTOCOL_INIT",
      "LWS_CALLBACK_PROTOCOL_DESTROY",
      "LWS_CALLBACK_WSI_CREATE",
      "LWS_CALLBACK_WSI_DESTROY",
      "LWS_CALLBACK_GET_THREAD_ID",
      "LWS_CALLBACK_ADD_POLL_FD",
      "LWS_CALLBACK_DEL_POLL_FD",
      "LWS_CALLBACK_CHANGE_MODE_POLL_FD",
      "LWS_CALLBACK_LOCK_POLL",
      "LWS_CALLBACK_UNLOCK_POLL",
      "LWS_CALLBACK_OPENSSL_CONTEXT_REQUIRES_PRIVATE_KEY",
      "LWS_CALLBACK_WS_PEER_INITIATED_CLOSE",
      "LWS_CALLBACK_WS_EXT_DEFAULTS",
      "LWS_CALLBACK_CGI",
      "LWS_CALLBACK_CGI_TERMINATED",
      "LWS_CALLBACK_CGI_STDIN_DATA",
      "LWS_CALLBACK_CGI_STDIN_COMPLETED",
      "LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP",
      "LWS_CALLBACK_CLOSED_CLIENT_HTTP",
      "LWS_CALLBACK_RECEIVE_CLIENT_HTTP",
      "LWS_CALLBACK_COMPLETED_CLIENT_HTTP",
      "LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ",
      "LWS_CALLBACK_HTTP_BIND_PROTOCOL",
      "LWS_CALLBACK_HTTP_DROP_PROTOCOL",
      "LWS_CALLBACK_CHECK_ACCESS_RIGHTS",
      "LWS_CALLBACK_PROCESS_HTML",
      "LWS_CALLBACK_ADD_HEADERS",
      "LWS_CALLBACK_SESSION_INFO",
      "LWS_CALLBACK_GS_EVENT",
      "LWS_CALLBACK_HTTP_PMO",
      "LWS_CALLBACK_CLIENT_HTTP_WRITEABLE",
      "LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION",
      "LWS_CALLBACK_RAW_RX",
      "LWS_CALLBACK_RAW_CLOSE",
      "LWS_CALLBACK_RAW_WRITEABLE",
      "LWS_CALLBACK_RAW_ADOPT",
      "LWS_CALLBACK_RAW_ADOPT_FILE",
      "LWS_CALLBACK_RAW_RX_FILE",
      "LWS_CALLBACK_RAW_WRITEABLE_FILE",
      "LWS_CALLBACK_RAW_CLOSE_FILE",
      "LWS_CALLBACK_SSL_INFO",
      0,
      "LWS_CALLBACK_CHILD_CLOSING",
      "LWS_CALLBACK_CGI_PROCESS_ATTACH",
      "LWS_CALLBACK_EVENT_WAIT_CANCELLED",
      "LWS_CALLBACK_VHOST_CERT_AGING",
      "LWS_CALLBACK_TIMER",
      "LWS_CALLBACK_VHOST_CERT_UPDATE",
      "LWS_CALLBACK_CLIENT_CLOSED",
      "LWS_CALLBACK_CLIENT_HTTP_DROP_PROTOCOL",
      "LWS_CALLBACK_WS_SERVER_BIND_PROTOCOL",
      "LWS_CALLBACK_WS_SERVER_DROP_PROTOCOL",
      "LWS_CALLBACK_WS_CLIENT_BIND_PROTOCOL",
      "LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL",
      "LWS_CALLBACK_RAW_SKT_BIND_PROTOCOL",
      "LWS_CALLBACK_RAW_SKT_DROP_PROTOCOL",
      "LWS_CALLBACK_RAW_FILE_BIND_PROTOCOL",
      "LWS_CALLBACK_RAW_FILE_DROP_PROTOCOL",
      "LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL",
      "LWS_CALLBACK_HTTP_CONFIRM_UPGRADE",
      0,
      0,
      "LWS_CALLBACK_RAW_PROXY_CLI_RX",
      "LWS_CALLBACK_RAW_PROXY_SRV_RX",
      "LWS_CALLBACK_RAW_PROXY_CLI_CLOSE",
      "LWS_CALLBACK_RAW_PROXY_SRV_CLOSE",
      "LWS_CALLBACK_RAW_PROXY_CLI_WRITEABLE",
      "LWS_CALLBACK_RAW_PROXY_SRV_WRITEABLE",
      "LWS_CALLBACK_RAW_PROXY_CLI_ADOPT",
      "LWS_CALLBACK_RAW_PROXY_SRV_ADOPT",
      "LWS_CALLBACK_RAW_PROXY_CLI_BIND_PROTOCOL",
      "LWS_CALLBACK_RAW_PROXY_SRV_BIND_PROTOCOL",
      "LWS_CALLBACK_RAW_PROXY_CLI_DROP_PROTOCOL",
      "LWS_CALLBACK_RAW_PROXY_SRV_DROP_PROTOCOL",
      "LWS_CALLBACK_RAW_CONNECTED",
      "LWS_CALLBACK_VERIFY_BASIC_AUTHORIZATION",
      "LWS_CALLBACK_WSI_TX_CREDIT_GET",
      "LWS_CALLBACK_CLIENT_HTTP_REDIRECT",
      "LWS_CALLBACK_CONNECTING",
  })[reason];
}

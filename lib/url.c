/**
 * @file url.c
 */
#include "url.h"
#include "js-utils.h"
#include "lws-utils.h"
#include "utils.h"
#include "query.h"
#include <assert.h>
#include <limits.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

char* strdup(const char*);

#ifndef HAVE_STRLCPY
size_t
strlcpy(char* dst, const char* src, size_t siz) {
  register char* d = dst;
  register const char* s = src;
  register size_t n = siz;

  if(n != 0 && --n != 0) {
    do {
      if((*d++ = *s++) == 0)
        break;
    } while(--n != 0);
  }

  if(n == 0) {
    if(siz != 0)
      *d = '\0';
    while(*s++)
      ;
  }

  return (s - src - 1);
}

#endif

static char const* const protocol_names[] = {
    "ws",
    "wss",
    "http",
    "https",
    "raw",
    "tls",
};

enum protocol
protocol_number(const char* protocol) {
  int i;

  for(i = countof(protocol_names) - 1; i >= 0; --i)
    if(!strcasecmp(protocol, protocol_names[i]))
      return i;

  return PROTOCOL_RAW;
}

const char*
protocol_string(enum protocol p) {
  int i = (unsigned int)p;
  assert(i >= 0);
  assert(i < (int)countof(protocol_names));
  return protocol_names[i];
}

uint16_t
protocol_default_port(enum protocol p) {
  switch(p) {
    case PROTOCOL_WS:
    case PROTOCOL_HTTP: {
      return 80;
    }

    case PROTOCOL_WSS:
    case PROTOCOL_HTTPS: {
      return 443;
    }
    default: {
      return 0;
    }
  }
}

BOOL
protocol_is_tls(enum protocol p) {
  switch(p) {
    case PROTOCOL_WSS:
    case PROTOCOL_HTTPS:
    case PROTOCOL_TLS: {
      return TRUE;
    }
    default: {
      return FALSE;
    }
  }
}

static const char*
start_from(const char* p, char ch) {
  if(p) {
    for(; *p; p++) {
      if(*p == '\\')
        ++p;
      else if(*p == ch)
        break;
    }
  }
  return p;
}

void
url_init(URL* url, const char* protocol, const char* host, int port, const char* path, JSContext* ctx) {
  enum protocol proto = protocol ? protocol_number(protocol) : -1;

  url->protocol = protocol ? protocol_string(proto) : 0;
  url->host = js_strdup(ctx, host && *host ? host : "0.0.0.0");
  url->port = URL_IS_VALID_PORT(port) ? port : protocol_default_port(proto);
  url->path = js_strdup(ctx, path ? path : "");
}

void
url_parse(URL* url, const char* u, JSContext* ctx) {
  enum protocol proto = PROTOCOL_WS;
  const char *s, *t;

  if((s = strstr(u, "://"))) {
    url->protocol = js_strndup(ctx, u, s - u);
    proto = protocol_number(url->protocol);
    u = s + 3;
  } else {
    url->protocol = 0;
  }

  for(s = u; *s; ++s)
    if(*s == ':' || *s == '/')
      break;

  url->host = s > u ? js_strndup(ctx, u, s - u) : 0;

  if(*s == ':') {
    unsigned long n = strtoul(++s, (char**)&t, 10);

    url->port = n != ULONG_MAX ? n : 0;
    if(s < t)
      s = t;
  } else if(url->protocol) {
    url->port = protocol_default_port(proto);
  } else {
    url->port = -1;
  }

  // if(!url->path)
  if(s && *s)
    url->path = s && *s ? js_strdup(ctx, s) : 0;
}

size_t
url_print(char* buf, size_t size, const URL url) {
  size_t pos = 0;

  if(url.protocol && *url.protocol) {
    if((pos += buf ? strlcpy(buf, url.protocol, size - pos) : strlen(url.protocol)) + 3 > size)
      return pos;

    if((pos += buf ? strlcpy(&buf[pos], "://", size - pos) : 3) >= size)
      return pos;
  }

  if(url.host) {
    if((pos += buf ? strlcpy(&buf[pos], url.host, size - pos) : strlen(url.host)) >= size)
      return pos;
  }

  if(url.port) {
    if(size - pos < 7)
      return pos;
    if(!buf) {
      pos += 1 + (url.port > 9999 ? 5 : url.port > 999 ? 4 : url.port > 99 ? 3 : url.port > 9 ? 2 : 1);
    } else {
      sprintf(&buf[pos], ":%u", url.port);
      pos += strlen(&buf[pos]);
    }
  }

  if(url.path) {
    if((pos += buf ? strlcpy(&buf[pos], url.path, size - pos) : strlen(url.path)) >= size)
      return pos;
  }

  return pos;
}

char*
url_format(const URL url, JSContext* ctx) {
  size_t len = (url.protocol ? strlen(url.protocol) + 3 : 0) + (url.host ? strlen(url.host) + 1 + 5 : 0) + (url.path ? strlen(url.path) : 0) + 1;
  char* str;
  enum protocol proto = -1;

  if((str = js_malloc(ctx, len))) {
    size_t pos = 0;
    const char* host;
    str[pos] = '\0';
    if(url.protocol) {
      proto = protocol_number(url.protocol);
      strcpy(str, url.protocol);
      pos += strlen(str);
      strcpy(&str[pos], "://");
      pos += 3;
    }
    if((host = url.host ? url.host : (pos > 0 || url.port >= 0) ? "0.0.0.0" : 0)) {
      if((int)proto != -1 && (url.port == protocol_default_port(proto) || url.port < 0)) {
        strcpy(&str[pos], host);
        pos += strlen(&str[pos]);
      } else {
        pos += sprintf(&str[pos], "%s:%u", host, url.port);
      }
    }
    if(url.path)
      strcpy(&str[pos], url.path);
  }
  return str;
}

char*
url_host(const URL url, JSContext* ctx) {
  size_t len = (url.host ? strlen(url.host) + 1 + 5 : 0) + 1;
  char* str;

  if((str = js_malloc(ctx, len))) {
    size_t pos = 0;
    const char* host;
    str[pos] = '\0';

    if((host = url.host ? url.host : (pos > 0 || url.port >= 0) ? "0.0.0.0" : 0)) {
      if(url.port < 0) {
        strcpy(&str[pos], host);
        pos += strlen(&str[pos]);
      } else {
        pos += sprintf(&str[pos], "%s:%u", host, url.port);
      }
    }
  }
  return str;
}

size_t
url_length(const URL url) {
  return url_print(0, 8192, url);
  /*size_t portlen = url.port >= 10000 ? 6 : url.port >= 1000 ? 5 : url.port >= 100 ? 4 : url.port >= 10 ? 3 : url.port >= 1 ? 2 : 0;
  return (url.protocol ? strlen(url.protocol) + 3 : 0) + (url.host ? strlen(url.host) + portlen : 0) + (url.path ? strlen(url.path) : 0) + 1;*/
}

void
url_free(URL* url, JSRuntime* rt) {
  if(url->host)
    js_free_rt(rt, url->host);

  if(url->path)
    js_free_rt(rt, url->path);

  memset(url, 0, sizeof(URL));
}

enum protocol
url_set_protocol(URL* url, const char* proto) {
  enum protocol p = protocol_number(proto);

  url->protocol = protocol_string(p);
  return p;
}

BOOL
url_set_path_len(URL* url, const char* path, size_t len, JSContext* ctx) {
  char* oldpath = url->path;
  BOOL ret = FALSE;

  if(has_query_b(path, len)) {
    ret = !!(url->path = js_strndup(ctx, path, len));
  } else {
    const char* oldquery;

    if((oldquery = url_query(*url))) {
      if((url->path = js_malloc(ctx, len + 1 + strlen(oldquery) + 1))) {
        memcpy(url->path, path, len);
        url->path[len] = '?';
        strcpy(&url->path[len + 1], oldquery);
      }
    } else {
      url->path = js_strndup(ctx, path, len);
      ret = !!url->path;
    }
  }
  js_free(ctx, oldpath);
  return ret;
}

BOOL
url_set_query_len(URL* url, const char* query, size_t len, JSContext* ctx) {
  size_t pathlen;
  char* oldquery;

  if(url->path && (oldquery = strchr(url->path, '?'))) {
    pathlen = oldquery - url->path;
  } else {
    pathlen = url->path ? strlen(url->path) : 0;
  }
  if((url->path = js_realloc(ctx, url->path, pathlen + 1 + len + 1))) {
    url->path[pathlen] = '?';
    memcpy(&url->path[pathlen + 1], query, len);
    url->path[pathlen + 1 + len] = '\0';
    return TRUE;
  }
  return FALSE;
}

void
url_info(const URL url, struct lws_client_connect_info* info) {
  enum protocol proto = url.protocol ? protocol_number(url.protocol) : PROTOCOL_RAW;

  memset(info, 0, sizeof(struct lws_client_connect_info));

  switch(proto) {
    case PROTOCOL_HTTP:
    case PROTOCOL_HTTPS: {
#if defined(LWS_ROLE_H2) && defined(LWS_ROLE_H1)
      info->alpn = "h2,http/1.1";
#elif defined(LWS_ROLE_H2)
      info->alpn = "h2";
#elif defined(LWS_ROLE_H1)
      info->alpn = "http/1.1";
#endif
      info->protocol = strdup("http");
      break;
    }

    case PROTOCOL_WS:
    case PROTOCOL_WSS: {
      info->protocol = strdup("ws");
      break;
    }
    default: {
      info->method = "RAW";
      info->local_protocol_name = "raw";
      break;
    }
  }

  info->port = url.port;
  info->address = url.host;

  if(protocol_is_tls(proto)) {
    info->ssl_connection = LCCSCF_USE_SSL;
    info->ssl_connection |= LCCSCF_ALLOW_SELFSIGNED;
    info->ssl_connection |= LCCSCF_ALLOW_INSECURE;
    info->ssl_connection |= LCCSCF_ALLOW_EXPIRED;
    info->ssl_connection |= LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
#ifdef LWS_ROLE_H2
    info->ssl_connection |= LCCSCF_H2_QUIRK_OVERFLOWS_TXCR | LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM;
#endif
  }

  info->path = url.path ? url.path : "/";
  info->host = info->address;
  info->origin = info->address;
}

const char*
url_query(const URL url) {
  const char* p;

  if(*(p = start_from(url.path, '?')) == '?')
    ++p;
  else
    p = 0;

  return p;
}

const char*
url_search(const URL url, size_t* len_p) {
  const char* s;

  if((s = start_from(url.path, '?')))
    if(len_p)
      *len_p = str_chr(s, '#');

  return s;
}

const char*
url_hash(const URL url) {
  return start_from(url.path, '#');
}

void
url_fromobj(URL* url, JSValueConst obj, JSContext* ctx) {
  const char *protocol, *host, *path;
  int32_t port = -1;

  protocol = js_get_propertystr_cstring(ctx, obj, "protocol");

  if(!(host = js_get_propertystr_cstring(ctx, obj, "hostname")))
    host = js_get_propertystr_cstring(ctx, obj, "host");

  if(js_has_propertystr(ctx, obj, "port"))
    port = js_get_propertystr_uint32(ctx, obj, "port");

  if(!(path = js_get_propertystr_cstring(ctx, obj, "pathname")))
    path = js_get_propertystr_cstring(ctx, obj, "path");

  url_init(url, protocol, host, port, path, ctx);

  if(protocol)
    JS_FreeCString(ctx, protocol);
  if(host)
    JS_FreeCString(ctx, host);
  if(path)
    JS_FreeCString(ctx, path);
}

BOOL
url_fromvalue(URL* url, JSValueConst value, JSContext* ctx) {
  URL* other;

  if(JS_IsObject(value)) {
    url_fromobj(url, value, ctx);
  } else if(JS_IsString(value)) {
    const char* str = JS_ToCString(ctx, value);
    url_parse(url, str, ctx);
    JS_FreeCString(ctx, str);
  } else {
    return FALSE;
  }

  return TRUE;
}

void
url_fromwsi(URL* url, struct lws* wsi, JSContext* ctx) {
  int i, port = -1;
  char* p;
  const char* protocol;
  typedef char* get_host_and_port(struct lws*, int*);
  get_host_and_port* fns[] = {
      &wsi_host_and_port,
      &wsi_vhost_and_port,
  };

  for(i = 0; i < (int)countof(fns); i++) {
    if((p = fns[i](wsi, &port))) {
      if(p[0]) {
        if(url->host)
          js_free(ctx, url->host);
        url->host = p;
        if(URL_IS_VALID_PORT(port)) {
          url->port = port;
          break;
        }
      } else {
        js_free(ctx, p);
      }
    }
  }

  if((p = wsi_uri_and_method(wsi, 0))) {
    if(url->path)
      free(url->path);
    url->path = p;
  }

  assert(url->path);

  if(url_query(*url) == NULL) {
    char* q;
    size_t qlen;
    if((q = wsi_query_string_len(wsi, &qlen))) {
      url_set_query_len(url, q, qlen, ctx);
      free(q);
    }
  }

  if((protocol = wsi_protocol_name(wsi))) {
    enum protocol number = protocol_number(protocol);

    if(number >= 0 && number < NUM_PROTOCOLS) {
      if(!protocol_is_tls(number)) {
        if(wsi_tls(wsi))
          number++;
      }

      url->protocol = protocol_string(number);
    }
  }

  // url->query = minnet_query_string(wsi, ctx);
}

URL*
url_new(JSContext* ctx) {

  URL* url;

  if(!(url = js_mallocz(ctx, sizeof(URL))))
    return url;

  url->ref_count = 1;
  return url;
}

JSValue
url_object(const URL url, JSContext* ctx) {
  JSValue ret = JS_NewObject(ctx);

  if(url.protocol)
    JS_SetPropertyStr(ctx, ret, "protocol", JS_NewString(ctx, url.protocol));

  if(url.host)
    JS_SetPropertyStr(ctx, ret, "hostname", JS_NewString(ctx, url.host));

  if(URL_IS_VALID_PORT(url.port))
    JS_SetPropertyStr(ctx, ret, "port", JS_NewUint32(ctx, url.port));

  if(url.path) {
    size_t len = str_chrs(url.path, "?#", 2);
    JS_SetPropertyStr(ctx, ret, "pathname", JS_NewStringLen(ctx, url.path, len));

    const char* str;
    if((str = url_search(url, &len)))
      JS_SetPropertyStr(ctx, ret, "search", JS_NewStringLen(ctx, str, len));
    if((str = url_hash(url)))
      JS_SetPropertyStr(ctx, ret, "hash", JS_NewString(ctx, str));
  }

  return ret;
}

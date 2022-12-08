#include "url.h"
#include "../minnet-url.h"
#include "jsutils.h"
#include "utils.h"
#include "query.h"
#include <assert.h>
#include <limits.h>
#include <ctype.h>
#include <strings.h>

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
  assert(i < countof(protocol_names));
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

void
url_init(struct url* url, const char* protocol, const char* host, int port, const char* path, JSContext* ctx) {
  enum protocol proto = protocol_number(protocol);

  url->protocol = protocol_string(proto);
  url->host = js_strdup(ctx, host && *host ? host : "0.0.0.0");
  url->port = port >= 0 && port <= 65535 ? port : protocol_default_port(proto);
  url->path = js_strdup(ctx, path ? path : "");
}

void
url_parse(struct url* url, const char* u, JSContext* ctx) {
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

struct url
url_create(const char* str, JSContext* ctx) {
  struct url ret = {1, 0, 0, 0, 0};
  url_parse(&ret, str, ctx);
  return ret;
}

size_t
url_print(char* buf, size_t size, const struct url url) {
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
url_format(const struct url url, JSContext* ctx) {
  size_t len = (url.protocol ? strlen(url.protocol) + 3 : 0) + (url.host ? strlen(url.host) + 1 + 5 : 0) + (url.path ? strlen(url.path) : 0) + 1;
  char* str;
  enum protocol proto = -1;

  if((str = js_malloc(ctx, len))) {
    const char* host = url.host ? url.host : "0.0.0.0";
    size_t pos = 0;
    str[pos] = '\0';
    if(url.protocol) {
      proto = protocol_number(url.protocol);
      strcpy(str, url.protocol);
      pos += strlen(str);
      strcpy(&str[pos], "://");
      pos += 3;
    }
    if(proto != -1 && url.port == protocol_default_port(proto)) {
      strcpy(&str[pos], host);
      pos += strlen(&str[pos]);
    } else {
      pos += sprintf(&str[pos], "%s:%u", host, url.port);
    }
    if(url.path)
      strcpy(&str[pos], url.path);
  }
  return str;
}

size_t
url_length(const struct url url) {
  return url_print(0, 8192, url);
  /*size_t portlen = url.port >= 10000 ? 6 : url.port >= 1000 ? 5 : url.port >= 100 ? 4 : url.port >= 10 ? 3 : url.port >= 1 ? 2 : 0;
  return (url.protocol ? strlen(url.protocol) + 3 : 0) + (url.host ? strlen(url.host) + portlen : 0) + (url.path ? strlen(url.path) : 0) + 1;*/
}

void
url_free(struct url* url, JSContext* ctx) {
  if(url->host)
    js_free(ctx, url->host);
  if(url->path)
    js_free(ctx, url->path);
  memset(url, 0, sizeof(struct url));
}

void
url_free_rt(struct url* url, JSRuntime* rt) {
  if(url->host)
    js_free_rt(rt, url->host);
  if(url->path)
    js_free_rt(rt, url->path);
  memset(url, 0, sizeof(struct url));
}

enum protocol
url_set_protocol(struct url* url, const char* proto) {
  enum protocol p = protocol_number(proto);

  url->protocol = protocol_string(p);
  return p;
}

BOOL
url_set_path_len(struct url* url, const char* path, size_t len, JSContext* ctx) {
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
url_set_query_len(struct url* url, const char* query, size_t len, JSContext* ctx) {
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
url_info(const struct url url, struct lws_client_connect_info* info) {
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
      info->protocol = "http";
      break;
    }
    case PROTOCOL_WS:
    case PROTOCOL_WSS: {
      info->protocol = "ws";
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
    info->ssl_connection |= LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
#ifdef LWS_ROLE_H2
    info->ssl_connection |= LCCSCF_H2_QUIRK_OVERFLOWS_TXCR | LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM;
#endif
  }

  info->path = url.path ? url.path : "/";
  info->host = info->address;
  info->origin = info->address;
}

/*int
url_connect(struct url* url, struct lws_context* context, struct lws** p_wsi) {
  struct lws_client_connect_info i;

  url_info(url, &i);

  i.context = context;
  i.pwsi = p_wsi;

  return !lws_client_connect_via_info(&i);
}*/

char*
url_location(const struct url url, JSContext* ctx) {
  const char* query;
  if((query = url_query(url)))
    return js_strndup(ctx, url.path, query - url.path);
  return js_strdup(ctx, url.path);
}

const char*
url_query(const struct url url) {
  const char* p;
  if(!url.path)
    return 0;

  for(p = url.path; *p; p++) {
    if(*p == '\\') {
      ++p;
      continue;
    }
    if(*p == '?') {
      ++p;
      break;
    }
  }
  return *p ? p : 0;
}

void
url_fromobj(struct url* url, JSValueConst obj, JSContext* ctx) {
  JSValue value;
  const char *protocol, *host, *path;
  int32_t port = -1;

  value = JS_GetPropertyStr(ctx, obj, "protocol");
  protocol = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, obj, "host");
  host = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, obj, "port");
  JS_ToInt32(ctx, &port, value);

  value = JS_GetPropertyStr(ctx, obj, "path");
  path = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  url_init(url, protocol, host, port, path, ctx);

  if(protocol)
    JS_FreeCString(ctx, protocol);
  if(host)
    JS_FreeCString(ctx, host);
  if(path)
    JS_FreeCString(ctx, path);
}

BOOL
url_fromvalue(struct url* url, JSValueConst value, JSContext* ctx) {
  struct url* other;

  if((other = minnet_url_data(value))) {
    url_copy(url, *other, ctx);
  } else if(JS_IsObject(value)) {
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
url_fromwsi(struct url* url, struct lws* wsi, JSContext* ctx) {
  int i, port = -1;
  char* p;
  const char* protocol;
  typedef char* get_host_and_port(struct lws*, JSContext*, int*);
  get_host_and_port* fns[] = {
      &wsi_host_and_port,
      &wsi_vhost_and_port,
  };

  for(i = 0; i < countof(fns); i++) {
    if((p = fns[i](wsi, ctx, &port))) {
      if(p[0]) {
        if(url->host)
          js_free(ctx, url->host);
        url->host = p;
        if(port >= 0 && port <= 65535) {
          url->port = port;
          break;
        }
      } else {
        js_free(ctx, p);
      }
    }
  }

  if((p = wsi_uri_and_method(wsi, ctx, 0))) {
    if(url->path)
      js_free(ctx, url->path);
    url->path = p;
  }

  assert(url->path);

  if(url_query(*url) == NULL) {
    char* q;
    size_t qlen;
    if((q = wsi_query_string_len(wsi, &qlen, ctx))) {
      url_set_query_len(url, q, qlen, ctx);
      js_free(ctx, q);
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

void
url_dump(const char* n, struct url const* url) {
  fprintf(stderr, "%s{ protocol = %s, host = %s, port = %u, path = %s }\n", n, url->protocol, url->host, url->port, url->path);
  fflush(stderr);
}

struct url*
url_new(JSContext* ctx) {

  struct url* url;

  if(!(url = js_mallocz(ctx, sizeof(struct url))))
    return url;

  url->ref_count = 1;
  return url;
}

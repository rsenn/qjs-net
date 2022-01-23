#include "minnet-url.h"
#include <cutils.h>

MinnetURL
url_init(JSContext* ctx, const char* protocol, const char* host, uint16_t port, const char* path) {
  MinnetURL url;
  url.protocol = protocol ? js_strdup(ctx, protocol) : 0;
  url.host = host ? js_strdup(ctx, host) : 0;
  url.port = port;
  url.path = path ? js_strdup(ctx, path) : 0;
  return url;
}

MinnetURL
url_parse(JSContext* ctx, const char* url) {
  MinnetURL ret = {0, 0, -1, 0};
  size_t i = 0, j;
  char* end;
  if((end = strstr(url, "://"))) {
    i = end - url;
    ret.protocol = js_strndup(ctx, url, i);
    i += 3;
  }
  for(j = i; url[j]; j++) {
    if(url[j] == ':' || url[j] == '/')
      break;
  }
  if(j - i)
    ret.host = js_strndup(ctx, &url[i], j - i);
  i = url[j] ? j + 1 : j;
  if(url[j] == ':') {
    unsigned long n = strtoul(&url[i], &end, 10);
    if((j = end - url) > i)
      ret.port = n;
  }
  if(url[j])
    ret.path = js_strdup(ctx, &url[j]);
  return ret;
}

char*
url_format(const MinnetURL* url, JSContext* ctx) {
  size_t len = strlen(url->protocol) + 3 + strlen(url->host) + 1 + 5 + 1 + strlen(url->path) + 1;
  char* buf = js_malloc(ctx, len);
  snprintf(buf, len, "%s://%s:%u/%s", url->protocol, url->host, url->port, url->path);
  return buf;
}

void
url_free(JSContext* ctx, MinnetURL* url) {
  if(url->protocol)
    js_free(ctx, url->protocol);
  if(url->host)
    js_free(ctx, url->host);
  if(url->path)
    js_free(ctx, url->path);
  memset(url, 0, sizeof(MinnetURL));
}

int
url_connect(MinnetURL* url, struct lws_context* context, struct lws** p_wsi) {
  struct lws_client_connect_info i;
  BOOL ssl = FALSE;

  memset(&i, 0, sizeof(i));

  if(url->protocol && !strncmp(url->protocol, "raw", 3)) {
    i.method = "RAW";
    i.local_protocol_name = "raw";
  } else if(url->protocol && !strncmp(url->protocol, "http", 4)) {
    i.alpn = "http/1.1";
    i.method = "GET";
    i.protocol = "http";
  } else {
    i.protocol = "ws";
  }

  if(url->protocol && !strncmp(url->protocol, "https", 5) && !strncmp(url->protocol, "wss", 3))
    ssl = TRUE;

  i.context = context;
  i.port = url->port;
  i.address = url->host;

  if(ssl) {
    i.ssl_connection = LCCSCF_USE_SSL | LCCSCF_H2_QUIRK_OVERFLOWS_TXCR | LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM;
    i.ssl_connection |= LCCSCF_ALLOW_SELFSIGNED;
    i.ssl_connection |= LCCSCF_ALLOW_INSECURE;
  }

  i.path = url->path;
  i.host = i.address;
  i.origin = i.address;
  i.pwsi = p_wsi;

  url->host = 0;
  url->path = 0;

  return !lws_client_connect_via_info(&i);
}

char*
url_path(const MinnetURL* url, JSContext* ctx) {
  const char* query;
  if((query = url_query_string(url)))
    return js_strndup(ctx, url->path, query - url->path);
  return js_strdup(ctx, url->path);
}

const char*
url_query_string(const MinnetURL* url) {

  const char* p;

  for(p = url->path; *p; p++) {
    if(*p == '\\') {
      ++p;
      continue;
    }
    if(*p == '?')
      break;
  }
  return *p ? p : 0;
}

JSValue
url_query_object(const MinnetURL* url, JSContext* ctx) {
  const char *p, *q;
  JSValue ret = JS_NewObject(ctx);

  if((q = url_query(url))) {
    size_t i, n = strlen(q);
    for(i = 0, p = q; i <= n; i++, q++) {
      if(*q == '\\') {
        ++q;
        continue;
      }
      if(*p == '&' || *p == '\0') {
        size_t namelen, len = p - q;
        char *value, *decoded;
        JSAtom atom;
        if((value = strchr(q, '='))) {
          namelen = value - q;
          ++value;
          atom = JS_NewAtomLen(ctx, q, namelen);
          decoded = js_strndup(ctx, value, p - value);
          lws_urldecode(decoded, decoded, p - value + 1);
          JS_SetProperty(ctx, ret, atom, JS_NewString(ctx, decoded));
          JS_FreeAtom(ctx, atom);
        }
      }
    }
  }
  return ret;
}

char*
url_query_from(JSContext* ctx, JSValueConst obj) {
  JSPropertyEnum* tab;
  uint32_t tab_len, i;
  DynBuf out;
  dbuf_init2(&out, ctx, (DynBufReallocFunc*)js_realloc);

  if(JS_GetOwnPropertyNames(ctx, &tab, &tab_len, obj, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK))
    return -1;

  for(i = 0; i < tab_len; i++) {
    JSValue value = JS_GetProperty(ctx, obj, tab[i].atom);
    size_t len;
    const char *prop, *str;

    str = JS_ToCStringLen(ctx, &len, value);
    prop = JS_AtomToCString(ctx, tab[i].atom);

    dbuf_putstr(&out, prop);
    dbuf_put(&out, "=", 1);
    dbuf_realloc(&out, out.size + (len * 3) + 1);

    lws_urlencode(&out.buf[out.size], str, out.allocated_size - out.size);
    out.size += strlen(&out.buf[out.size]);

    JS_FreeCString(ctx, prop);
    JS_FreeCString(ctx, str);

    JS_FreeValue(ctx, value);
  }

js_free(ctx, tab);

  return out.buf;
}

#include "minnet-url.h"
#include <quickjs.h>
#include <cutils.h>
#include <assert.h>

static char const* const protocol_names[] = {
    "ws",
    "wss",
    "http",
    "https",
    "raw",
    "tls",
};

MinnetProtocol
protocol_number(const char* protocol) {
  int i;

  for(i = countof(protocol_names) - 1; i >= 0; --i)
    if(!strcasecmp(protocol, protocol_names[i]))
      break;

  return i;
}

const char*
protocol_string(MinnetProtocol p) {
  int i = (unsigned int)p;
  assert(i >= 0);
  assert(i < countof(protocol_names));
  return protocol_names[i];
}

uint16_t
protocol_default_port(MinnetProtocol p) {
  switch(p) {
    case PROTOCOL_WS:
    case PROTOCOL_HTTP: return 80;
    case PROTOCOL_WSS:
    case PROTOCOL_HTTPS: return 443;
    default: return 0;
  }
}

BOOL
protocol_is_tls(MinnetProtocol p) {
  switch(p) {
    case PROTOCOL_WSS:
    case PROTOCOL_HTTPS:
    case PROTOCOL_TLS: return TRUE;
    default: return FALSE;
  }
}

MinnetURL
url_init(JSContext* ctx, const char* protocol, const char* host, int port, const char* path) {
  MinnetURL url;
  MinnetProtocol proto = protocol_number(protocol);

  url.protocol = protocol_string(proto);
  url.host = js_strdup(ctx, host && *host ? host : "0.0.0.0");
  url.port = port >= 0 && port <= 65535 ? port : protocol_default_port(proto);
  url.path = js_strdup(ctx, path && *path ? path : "/");

  return url;
}

MinnetURL
url_parse(JSContext* ctx, const char* u) {
  MinnetURL url = {0, 0, 0, 0};
  MinnetProtocol proto = PROTOCOL_WS;
  const char *s, *t;

  if((s = strstr(u, "://"))) {
    url.protocol = js_strndup(ctx, u, s - u);
    proto = protocol_number(url.protocol);
    u = s + 3;
  }

  for(s = u; *s; ++s)
    if(*s == ':' || *s == '/')
      break;

  if(s > u)
    url.host = js_strndup(ctx, u, s - u);

  if(*s == ':') {
    unsigned long n = strtoul(++s, &t, 10);

    url.port = n != ULONG_MAX ? n : 0;
    if(s < t)
      s = t;
  } else {
    url.port = protocol_default_port(proto);
  }

  url.path = js_strdup(ctx, s);
  return url;
}

char*
url_format(const MinnetURL* url, JSContext* ctx) {
  size_t len = strlen(url->protocol) + 3 + strlen(url->host) + 1 + 5 + strlen(url->path) + 1;
  char* str;
  MinnetProtocol proto = protocol_number(url->protocol);

  if((str = js_malloc(ctx, len))) {
    if(url->port == protocol_default_port(proto))
      sprintf(str, "%s://%s%s", url->protocol, url->host, url->path);
    else
      sprintf(str, "%s://%s:%u%s", url->protocol, url->host, url->port % 0xffff, url->path);
  }

  return str;
}

void
url_free(JSContext* ctx, MinnetURL* url) {
  if(url->host)
    js_free(ctx, url->host);
  if(url->path)
    js_free(ctx, url->path);
  memset(url, 0, sizeof(MinnetURL));
}

int
url_connect(MinnetURL* url, struct lws_context* context, struct lws** p_wsi) {
  struct lws_client_connect_info i;
  MinnetProtocol proto = protocol_number(url->protocol);

  memset(&i, 0, sizeof(i));

  switch(proto) {
    case PROTOCOL_HTTP:
    case PROTOCOL_HTTPS: {
      i.alpn = "http/1.1";
      i.method = "GET";
      i.protocol = "http";
      break;
    }
    case PROTOCOL_WS:
    case PROTOCOL_WSS: {
      i.protocol = "ws";
      break;
    }
    default: {
      i.method = "RAW";
      i.local_protocol_name = "raw";
      break;
    }
  }

  i.context = context;
  i.port = url->port;
  i.address = url->host;

  if(protocol_is_tls(proto)) {
    i.ssl_connection = LCCSCF_USE_SSL | LCCSCF_H2_QUIRK_OVERFLOWS_TXCR | LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM;
    i.ssl_connection |= LCCSCF_ALLOW_SELFSIGNED;
    i.ssl_connection |= LCCSCF_ALLOW_INSECURE;
  }

  i.path = url->path;
  i.host = i.address;
  i.origin = i.address;
  i.pwsi = p_wsi;

  return !lws_client_connect_via_info(&i);
}

char*
url_location(const MinnetURL* url, JSContext* ctx) {
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
    if(*p == '?') {
      ++p;
      break;
    }
  }
  return *p ? p : 0;
}

JSValue
url_query_object(const MinnetURL* url, JSContext* ctx) {
  const char *p, *q;
  JSValue ret = JS_NewObject(ctx);

  if((q = url_query_string(url))) {
    size_t i, n = strlen(q);
    for(i = 0, p = q; i <= n; i++, q++) {
      if(*q == '\\') {
        ++q;
        continue;
      }
      if(*p == '&' || *p == '\0') {
        size_t namelen, len;
        char *value, *decoded;
        JSAtom atom;
        if((value = strchr(q, '='))) {
          namelen = (const char*)value - q;
          ++value;
          atom = JS_NewAtomLen(ctx, q, namelen);
          len = p - (const char*)value;
          decoded = js_strndup(ctx, value, len);
          lws_urldecode(decoded, decoded, len + 1);
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

THREAD_LOCAL JSValue minnet_url_proto, minnet_url_ctor;
THREAD_LOCAL JSClassID minnet_url_class_id;

enum { URL_PROTOCOL, URL_HOST, URL_PORT, URL_PATH };

static JSValue
minnet_url_new(JSContext* ctx, struct lws* wsi) {
  MinnetURL* url;
  JSValue url_obj = JS_NewObjectProtoClass(ctx, minnet_url_proto, minnet_url_class_id);

  if(JS_IsException(url_obj))
    return JS_EXCEPTION;

  if(!(url = js_mallocz(ctx, sizeof(MinnetURL)))) {
    JS_FreeValue(ctx, url_obj);
    return JS_EXCEPTION;
  }

  url->protocol = 0;
  url->host = 0;
  url->port = 0;
  url->path = 0;

  JS_SetOpaque(url_obj, url);

  return url_obj;
}

static JSValue
minnet_url_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetURL* url;
  JSValue ret = JS_UNDEFINED;

  if(!(url = minnet_url_data(this_val)))
    return JS_UNDEFINED;

  switch(magic) {}
  return ret;
}

static JSValue
minnet_url_setter(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetURL* url;
  JSValue ret = JS_UNDEFINED;

  if(!(url = JS_GetOpaque2(ctx, this_val, minnet_url_class_id)))
    return JS_EXCEPTION;

  switch(magic) {}
  return ret;
}

JSValue
minnet_url_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetURL* url;

  if(!(url = js_mallocz(ctx, sizeof(MinnetURL))))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_url_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_url_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  url->protocol = 0;
  url->host = 0;
  url->port = 0;
  url->path = 0;

  JS_SetOpaque(obj, url);
  return obj;

fail:
  js_free(ctx, url);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
minnet_url_finalizer(JSRuntime* rt, JSValue val) {
  MinnetURL* url = JS_GetOpaque(val, minnet_url_class_id);
  if(url) {
    js_free_rt(rt, url);
  }
}

JSClassDef minnet_url_class = {
    "MinnetURL",
    .finalizer = minnet_url_finalizer,
};

const JSCFunctionListEntry minnet_url_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("protocol", minnet_url_getter, minnet_url_setter, URL_PROTOCOL, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("host", minnet_url_getter, minnet_url_setter, URL_HOST, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("port", minnet_url_getter, minnet_url_setter, URL_PORT, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("path", minnet_url_getter, minnet_url_setter, URL_PATH, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetURL", JS_PROP_CONFIGURABLE),

};

const size_t minnet_url_proto_funcs_size = countof(minnet_url_proto_funcs);

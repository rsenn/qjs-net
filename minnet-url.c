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
protocol_is_tls(MinnetProtocol p) {
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
url_init(MinnetURL* url, const char* protocol, const char* host, int port, const char* path, JSContext* ctx) {
  MinnetProtocol proto = protocol_number(protocol);

  url->protocol = protocol_string(proto);
  url->host = js_strdup(ctx, host && *host ? host : "0.0.0.0");
  url->port = port >= 0 && port <= 65535 ? port : protocol_default_port(proto);
  url->path = js_strdup(ctx, path ? path : "");
}

void
url_parse(MinnetURL* url, const char* u, JSContext* ctx) {
  MinnetProtocol proto = PROTOCOL_WS;
  const char *s, *t;

  if((s = strstr(u, "://"))) {
    url->protocol = js_strndup(ctx, u, s - u);
    proto = protocol_number(url->protocol);
    u = s + 3;
  }

  for(s = u; *s; ++s)
    if(*s == ':' || *s == '/')
      break;

  if(s > u)
    url->host = js_strndup(ctx, u, s - u);

  if(*s == ':') {
    unsigned long n = strtoul(++s, &t, 10);

    url->port = n != ULONG_MAX ? n : 0;
    if(s < t)
      s = t;
  } else {
    url->port = protocol_default_port(proto);
  }

  url->path = js_strdup(ctx, s);
}

char*
url_format(const MinnetURL* url, JSContext* ctx) {
  size_t len = (url->protocol ? strlen(url->protocol) + 3 : 0) + (url->host ? strlen(url->host) + 1 + 5 : 0) + (url->path ? strlen(url->path) : 0) + 1;
  char* str;
  MinnetProtocol proto = -1;

  if((str = js_malloc(ctx, len))) {
    const char* host = url->host ? url->host : "0.0.0.0";
    size_t pos = 0;
    str[pos] = '\0';
    if(url->protocol) {
      proto = protocol_number(url->protocol);
      strcpy(str, url->protocol);
      pos += strlen(str);
      strcpy(&str[pos], "://");
      pos += 3;
    }
    if(proto != -1 && url->port == protocol_default_port(proto)) {
      strcpy(&str[pos], host);
      pos += strlen(&str[pos]);
    } else {
      pos += sprintf(&str[pos], "%s:%u", host, url->port);
    }
    if(url->path)
      strcpy(&str[pos], url->path);
  }
  return str;
}

void
url_free(MinnetURL* url, JSContext* ctx) {
  if(url->host)
    js_free(ctx, url->host);
  if(url->path)
    js_free(ctx, url->path);
  memset(url, 0, sizeof(MinnetURL));
}

void
url_free_rt(MinnetURL* url, JSRuntime* rt) {
  if(url->host)
    js_free_rt(rt, url->host);
  if(url->path)
    js_free_rt(rt, url->path);
  memset(url, 0, sizeof(MinnetURL));
}

void
url_info(const MinnetURL* url, struct lws_client_connect_info* info) {
  MinnetProtocol proto = url->protocol ? protocol_number(url->protocol) : PROTOCOL_RAW;

  memset(info, 0, sizeof(struct lws_client_connect_info));

  switch(proto) {
    case PROTOCOL_HTTP:
    case PROTOCOL_HTTPS: {
      info->alpn = "http/1.1";
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

  info->port = url->port;
  info->address = url->host;

  if(protocol_is_tls(proto)) {
    info->ssl_connection = LCCSCF_USE_SSL | LCCSCF_H2_QUIRK_OVERFLOWS_TXCR | LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM;
    info->ssl_connection |= LCCSCF_ALLOW_SELFSIGNED;
    info->ssl_connection |= LCCSCF_ALLOW_INSECURE;
  }

  info->path = url->path ? url->path : "/";
  info->host = info->address;
  info->origin = info->address;
}

/*int
url_connect(MinnetURL* url, struct lws_context* context, struct lws** p_wsi) {
  struct lws_client_connect_info i;

  url_info(url, &i);

  i.context = context;
  i.pwsi = p_wsi;

  return !lws_client_connect_via_info(&i);
}*/

char*
url_location(const MinnetURL* url, JSContext* ctx) {
  const char* query;
  if((query = url_query(url)))
    return js_strndup(ctx, url->path, query - url->path);
  return js_strdup(ctx, url->path);
}

const char*
url_query(const MinnetURL* url) {
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

void
url_from(MinnetURL* url, JSValueConst obj, JSContext* ctx) {
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
}

JSValue
query_object(const char* q, JSContext* ctx) {
  const char* p;
  size_t i, n = strlen(q);
  JSValue ret = JS_NewObject(ctx);

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
  return ret;
}

char*
query_from(JSValueConst obj, JSContext* ctx) {
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

static THREAD_LOCAL JSValue minnet_url_proto, minnet_url_ctor;
THREAD_LOCAL JSClassID minnet_url_class_id;

enum { URL_PROTOCOL, URL_HOST, URL_PORT, URL_PATH, URL_TLS };

static JSValue
minnet_url_new(JSContext* ctx, MinnetURL u) {
  MinnetURL* url;
  JSValue url_obj = JS_NewObjectProtoClass(ctx, minnet_url_proto, minnet_url_class_id);

  if(JS_IsException(url_obj))
    return JS_EXCEPTION;

  if(!(url = js_mallocz(ctx, sizeof(MinnetURL)))) {
    JS_FreeValue(ctx, url_obj);
    return JS_EXCEPTION;
  }

  *url = u;

  JS_SetOpaque(url_obj, url);

  return url_obj;
}

static JSValue
minnet_url_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetURL* url;
  JSValue ret = JS_UNDEFINED;

  if(!(url = minnet_url_data(this_val)))
    return JS_UNDEFINED;

  switch(magic) {
    case URL_PROTOCOL: {
      ret = url->protocol ? JS_NewString(ctx, url->protocol) : JS_NULL;
      break;
    }
    case URL_HOST: {
      ret = url->host ? JS_NewString(ctx, url->host) : JS_NULL;
      break;
    }
    case URL_PORT: {
      ret = JS_NewUint32(ctx, url->port);
      break;
    }
    case URL_PATH: {
      ret = JS_NewString(ctx, url->path ? url->path : "");
      break;
    }
    case URL_TLS: {
      if(url->protocol) {
        MinnetProtocol proto = protocol_number(url->protocol);
        ret = JS_NewBool(ctx, protocol_is_tls(proto));
      }
      break;
    }
  }
  return ret;
}

static JSValue
minnet_url_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetURL* url;
  JSValue ret = JS_UNDEFINED;
  size_t len;
  const char* str;

  if(!(url = JS_GetOpaque2(ctx, this_val, minnet_url_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case URL_PROTOCOL: {
      if(JS_IsNull(value) || JS_IsUndefined(value)) {
        url->protocol = 0;
      } else if((str = JS_ToCString(ctx, value))) {
        MinnetProtocol proto = protocol_number(str);
        JS_FreeCString(ctx, str);
        url->protocol = protocol_string(proto);
      }
      break;
    }
    case URL_HOST: {
      /*if(JS_IsNull(value) || JS_IsUndefined(value)) {
        if(url->host)
          js_free(ctx, url->host);
        url->host = 0;
      } else */
      if((str = JS_ToCString(ctx, value))) {
        if(url->host)
          js_free(ctx, url->host);
        url->host = js_strdup(ctx, str);
        JS_FreeCString(ctx, str);
      }
      break;
    }
    case URL_PORT: {
      uint32_t port = 0;
      if(!JS_ToUint32(ctx, &port, value))
        if(port <= 65535)
          url->port = port;
      break;
    }
    case URL_PATH: {
      /* if(JS_IsNull(value) || JS_IsUndefined(value)) {
         if(url->path)
           js_free(ctx, url->path);
         url->path = 0;
       } else*/
      if((str = JS_ToCStringLen(ctx, &len, value))) {
        if(url->path)
          js_free(ctx, url->path);
        url->path = js_strndup(ctx, str, len);
        JS_FreeCString(ctx, str);
      }
      break;
    }
  }

  return ret;
}

enum { URL_TO_STRING };

JSValue
minnet_url_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MinnetURL* url;
  JSValue ret = JS_UNDEFINED;

  if(!(url = JS_GetOpaque2(ctx, this_val, minnet_url_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case URL_TO_STRING: {
      char* str;

      if((str = url_format(url, ctx))) {
        ret = JS_NewString(ctx, str);
        js_free(ctx, str);
      }
      break;
    }
  }
  return ret;
}

JSValue
minnet_url_from(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetURL url = {0};

  if(JS_IsObject(argv[0]))
    url_from(&url, argv[0], ctx);

  return minnet_url_new(ctx, url);
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

  if(JS_IsString(argv[0])) {
    const char* str;

    if((str = JS_ToCString(ctx, argv[0])))
      url_parse(url, str, ctx);
    JS_FreeCString(ctx, str);
  }

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
    url_free_rt(url, rt);
    js_free_rt(rt, url);
  }
}

static JSClassDef minnet_url_class = {
    "MinnetURL",
    .finalizer = minnet_url_finalizer,
};

static const JSCFunctionListEntry minnet_url_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("protocol", minnet_url_get, minnet_url_set, URL_PROTOCOL, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("host", minnet_url_get, minnet_url_set, URL_HOST, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("port", minnet_url_get, minnet_url_set, URL_PORT, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("path", minnet_url_get, minnet_url_set, URL_PATH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("tls", minnet_url_get, 0, URL_TLS, JS_PROP_ENUMERABLE),
    JS_CFUNC_MAGIC_DEF("toString", 0, minnet_url_method, URL_TO_STRING),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetURL", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry minnet_url_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, minnet_url_from),
};

int
minnet_url_init(JSContext* ctx, JSModuleDef* m) {

  // Add class URL
  JS_NewClassID(&minnet_url_class_id);
  JS_NewClass(JS_GetRuntime(ctx), minnet_url_class_id, &minnet_url_class);
  minnet_url_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_url_proto, minnet_url_proto_funcs, countof(minnet_url_proto_funcs));

  minnet_url_ctor = JS_NewCFunction2(ctx, minnet_url_constructor, "MinnetURL", 0, JS_CFUNC_constructor, 0);
  JS_SetPropertyFunctionList(ctx, minnet_url_ctor, minnet_url_static_funcs, countof(minnet_url_static_funcs));

  JS_SetConstructor(ctx, minnet_url_ctor, minnet_url_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "URL", minnet_url_ctor);

  return 0;
}

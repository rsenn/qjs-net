#include "minnet-url.h"
#include "jsutils.h"
#include "utils.h"
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

MinnetProtocol
protocol_number(const char* protocol) {
  int i;

  for(i = countof(protocol_names) - 1; i >= 0; --i)
    if(!strcasecmp(protocol, protocol_names[i]))
      return i;

  return PROTOCOL_RAW;
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
    url->port = 0;
  }

  // if(!url->path)
  if(s && *s)
    url->path = s && *s ? js_strdup(ctx, s) : 0;
}

MinnetURL
url_create(const char* str, JSContext* ctx) {
  MinnetURL ret = {1, 0, 0, 0, 0};
  url_parse(&ret, str, ctx);
  return ret;
}

size_t
url_print(char* buf, size_t size, const MinnetURL url) {
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
url_format(const MinnetURL url, JSContext* ctx) {
  size_t len = (url.protocol ? strlen(url.protocol) + 3 : 0) + (url.host ? strlen(url.host) + 1 + 5 : 0) + (url.path ? strlen(url.path) : 0) + 1;
  char* str;
  MinnetProtocol proto = -1;

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
url_length(const MinnetURL url) {
  return url_print(0, 8192, url);
  /*size_t portlen = url.port >= 10000 ? 6 : url.port >= 1000 ? 5 : url.port >= 100 ? 4 : url.port >= 10 ? 3 : url.port >= 1 ? 2 : 0;
  return (url.protocol ? strlen(url.protocol) + 3 : 0) + (url.host ? strlen(url.host) + portlen : 0) + (url.path ? strlen(url.path) : 0) + 1;*/
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

MinnetProtocol
url_set_protocol(MinnetURL* url, const char* proto) {
  MinnetProtocol p = protocol_number(proto);

  url->protocol = protocol_string(p);
  return p;
}

BOOL
url_set_path_len(MinnetURL* url, const char* path, size_t len, JSContext* ctx) {
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
url_set_query_len(MinnetURL* url, const char* query, size_t len, JSContext* ctx) {
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
url_info(const MinnetURL url, struct lws_client_connect_info* info) {
  MinnetProtocol proto = url.protocol ? protocol_number(url.protocol) : PROTOCOL_RAW;

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
    // info->ssl_connection |= LCCSCF_H2_QUIRK_OVERFLOWS_TXCR | LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM;
    info->ssl_connection |= LCCSCF_ALLOW_SELFSIGNED;
    info->ssl_connection |= LCCSCF_ALLOW_INSECURE;
    info->ssl_connection |= LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
  }

  info->path = url.path ? url.path : "/";
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
url_location(const MinnetURL url, JSContext* ctx) {
  const char* query;
  if((query = url_query(url)))
    return js_strndup(ctx, url.path, query - url.path);
  return js_strdup(ctx, url.path);
}

const char*
url_query(const MinnetURL url) {
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
url_fromobj(MinnetURL* url, JSValueConst obj, JSContext* ctx) {
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
url_fromvalue(MinnetURL* url, JSValueConst value, JSContext* ctx) {
  MinnetURL* other;

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
url_fromwsi(MinnetURL* url, struct lws* wsi, JSContext* ctx) {
  int len, port = -1;
  char* p;

  /*  if((len = lws_hdr_total_length(wsi, WSI_TOKEN_HOST))) {
      if(url->host)
        js_free(ctx, url->host);
      url->host = js_malloc(ctx, len + 1);
      lws_hdr_copy(wsi, url->host, len + 1, WSI_TOKEN_HOST);

      while(--len >= 0 && isdigit(url->host[len]))
        ;

      if(url->host[len] == ':') {
        url->port = atoi(&url->host[len + 1]);
        url->host[len] = '\0';
      }
    }*/

  if((p = wsi_host_and_port(wsi, ctx, &port))) {
    if(url->host)
      js_free(ctx, url->host);
    url->host = p;
    if(port != -1)
      url->port = port;
  }

  if((p = wsi_uri_and_method(wsi, ctx, 0))) {
    if(url->path)
      js_free(ctx, url->path);
    url->path = p;
    // lws_hdr_copy(wsi, url->path, len + 1, WSI_TOKEN_GET_URI);
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

  // url->query = minnet_query_string(wsi, ctx);
}

void
url_dump(const char* n, MinnetURL const* url) {
  fprintf(stderr, "%s{ protocol = %s, host = %s, port = %u, path = %s }\n", n, url->protocol, url->host, url->port, url->path);
  fflush(stderr);
}

static THREAD_LOCAL JSValue minnet_url_proto, minnet_url_ctor;
THREAD_LOCAL JSClassID minnet_url_class_id;

enum { URL_PROTOCOL, URL_HOST, URL_PORT, URL_PATH, URL_TLS };

JSValue
minnet_url_wrap(JSContext* ctx, MinnetURL* url) {
  JSValue url_obj = JS_NewObjectProtoClass(ctx, minnet_url_proto, minnet_url_class_id);

  if(JS_IsException(url_obj))
    return JS_EXCEPTION;
  /*
    if(!(url = js_mallocz(ctx, sizeof(MinnetURL)))) {
      JS_FreeValue(ctx, url_obj);
      return JS_EXCEPTION;
    }

    *url = u;
  */
  JS_SetOpaque(url_obj, url);

  return url_obj;
}

MinnetURL*
url_new(JSContext* ctx) {

  MinnetURL* url;

  if(!(url = js_mallocz(ctx, sizeof(MinnetURL))))
    return url;

  url->ref_count = 1;
  return url;
}

JSValue
minnet_url_new(JSContext* ctx, MinnetURL u) {
  MinnetURL* url;

  if(!(url = url_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  url_copy(url, u, ctx);
  return minnet_url_wrap(ctx, url);
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

  if(!(url = minnet_url_data2(ctx, this_val)))
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

  if(!(url = minnet_url_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case URL_TO_STRING: {
      char* str;

      if((str = url_format(*url, ctx))) {
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
  MinnetURL* url;

  if(!(url = url_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  url_fromvalue(url, argv[0], ctx);

  if(!url_valid(*url))
    return JS_ThrowTypeError(ctx, "Not asynciterator_shift valid URL");

  return minnet_url_wrap(ctx, url);
}

JSValue
minnet_url_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  DynBuf dbuf;
  char* str;
  MinnetURL* url;
  JSValue ret, options = argc >= 2 && JS_IsObject(argv[1]) ? JS_DupValue(ctx, argv[1]) : argc >= 1 && JS_IsObject(argv[0]) ? JS_DupValue(ctx, argv[0]) : JS_NewObject(ctx);
  JSValue opt_colors = JS_GetPropertyStr(ctx, options, "colors");
  BOOL colors = JS_IsUndefined(opt_colors) ? TRUE : JS_ToBool(ctx, opt_colors);
  JS_FreeValue(ctx, opt_colors);

  if(!(url = minnet_url_data2(ctx, this_val)))
    return JS_EXCEPTION;

  dbuf_init2(&dbuf, ctx, (DynBufReallocFunc*)js_realloc);

  dbuf_putstr(&dbuf, colors ? "\x1b[1;31mMinnetURL\x1b[0m" : "MinnetURL");
  if((str = url_format(*url, ctx))) {
    dbuf_printf(&dbuf, colors ? " \x1b[1;33m%s\x1b[0m" : " %s", str);
    js_free(ctx, str);
  }
  ret = JS_NewStringLen(ctx, (const char*)dbuf.buf, dbuf.size);
  dbuf_free(&dbuf);

  JS_FreeValue(ctx, options);

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
    JS_CGETSET_MAGIC_FLAGS_DEF("tls", minnet_url_get, 0, URL_TLS, 0),
    JS_CFUNC_MAGIC_DEF("toString", 0, minnet_url_method, URL_TO_STRING),
    JS_CFUNC_DEF("inspect", 0, minnet_url_inspect),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetURL", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry minnet_url_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, minnet_url_from),
};

int
minnet_url_init(JSContext* ctx, JSModuleDef* m) {
  JSAtom inspect_atom;

  // Add class URL
  JS_NewClassID(&minnet_url_class_id);
  JS_NewClass(JS_GetRuntime(ctx), minnet_url_class_id, &minnet_url_class);
  minnet_url_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_url_proto, minnet_url_proto_funcs, countof(minnet_url_proto_funcs));

  minnet_url_ctor = JS_NewCFunction2(ctx, minnet_url_constructor, "MinnetURL", 0, JS_CFUNC_constructor, 0);
  JS_SetPropertyFunctionList(ctx, minnet_url_ctor, minnet_url_static_funcs, countof(minnet_url_static_funcs));

  JS_SetConstructor(ctx, minnet_url_ctor, minnet_url_proto);

  if((inspect_atom = js_symbol_static_atom(ctx, "inspect")) >= 0) {
    JS_SetProperty(ctx, minnet_url_proto, inspect_atom, JS_NewCFunction(ctx, minnet_url_inspect, "inspect", 0));
    JS_FreeAtom(ctx, inspect_atom);
  }

  if(m)
    JS_SetModuleExport(ctx, m, "URL", minnet_url_ctor);

  return 0;
}

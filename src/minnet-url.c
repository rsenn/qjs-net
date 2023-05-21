#include "minnet-url.h"
#include "js-utils.h"
#include "utils.h"
#include "query.h"
#include <assert.h>
#include <limits.h>
#include <ctype.h>
#include <strings.h>

THREAD_LOCAL JSValue minnet_url_proto, minnet_url_ctor;
THREAD_LOCAL JSClassID minnet_url_class_id;

enum {
  URL_PROTOCOL,
  URL_HOSTNAME,
  URL_HOST,
  URL_PORT,
  URL_PATHNAME,
  URL_QUERY,
  URL_TLS,
  URL_SEARCH,
  URL_HASH,
  URL_ORIGIN,
  URL_HREF,
};

MinnetURL*
minnet_url_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_url_class_id);
}

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

JSValue
minnet_url_new(JSContext* ctx, MinnetURL u) {
  MinnetURL* url;

  if(!(url = url_new(ctx)))
    return JS_EXCEPTION;

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

    case URL_HOSTNAME: {
      ret = url->host ? JS_NewString(ctx, url->host) : JS_NULL;
      break;
    }

    case URL_HOST: {
      char* str;
      if((str = url_host(*url, ctx))) {
        ret = JS_NewString(ctx, str);
        js_free(ctx, str);
      }
      break;
    }

    case URL_PORT: {
      if(url->port >= 0 && url->port <= 0xffff)
        ret = JS_NewUint32(ctx, url->port);
      break;
    }

    case URL_PATHNAME: {
      ret = url->path ? JS_NewStringLen(ctx, url->path, str_chrs(url->path, "?#", 2)) : JS_NewString(ctx, "");
      break;
    }

    case URL_QUERY: {
      const char* query;
      if((query = url_query(*url)))
        ret = query_object(query, ctx);
      else
        ret = JS_NULL;
      break;
    }

    case URL_TLS: {
      if(url->protocol) {
        MinnetProtocol proto = protocol_number(url->protocol);
        ret = JS_NewBool(ctx, protocol_is_tls(proto));
      }
      break;
    }

    case URL_SEARCH: {
      const char* search;
      size_t searchlen;
      if((search = url_search(*url, &searchlen)))
        ret = JS_NewStringLen(ctx, search, searchlen);
      break;
    }

    case URL_HASH: {
      const char* hash;
      if((hash = url_hash(*url)))
        ret = JS_NewString(ctx, hash);
      break;
    }

    case URL_HREF: {
      char* str;
      if((str = url_format(*url, ctx))) {
        ret = JS_NewString(ctx, str);
        js_free(ctx, str);
      }
      break;
    }

    case URL_ORIGIN: {
      char *str, *end;
      if((str = url_format(*url, ctx))) {
        end = url->path ? strstr(str, url->path) : str + strlen(str);
        ret = JS_NewStringLen(ctx, str, end - str);
        js_free(ctx, str);
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

    case URL_HOSTNAME: {
      if((str = JS_ToCString(ctx, value))) {
        if(url->host)
          js_free(ctx, url->host);
        url->host = js_strdup(ctx, str);
        JS_FreeCString(ctx, str);
      }
      break;
    }

    case URL_PORT: {
      int32_t port = -1;
      JS_ToInt32(ctx, &port, value);
      url->port = URL_IS_VALID_PORT(port) ? port : -1;
      break;
    }

    case URL_PATHNAME: {
      if((str = JS_ToCStringLen(ctx, &len, value))) {
        url_set_path_len(url, str, len, ctx);
        JS_FreeCString(ctx, str);
      }
      break;
    }

    case URL_QUERY: {
      if(JS_IsString(value)) {
        if((str = JS_ToCStringLen(ctx, &len, value))) {
          url_set_query_len(url, str, len, ctx);
          JS_FreeCString(ctx, str);
        }
      } else if(JS_IsObject(value)) {
        char* query;
        if((query = query_from(value, ctx))) {
          url_set_query(url, query, ctx);
          js_free(ctx, query);
        }
      } else if(JS_IsNull(value) || JS_IsUndefined(value)) {
        size_t pathlen;
        if(url->path[(pathlen = str_chr(url->path, '?'))]) {
          url->path[pathlen] = '\0';
        }
      }
      break;
    }
  }
  return ret;
}

enum {
  URL_TO_STRING,
  URL_TO_OBJECT,
};

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
    case URL_TO_OBJECT: {
      ret = url_object(*url, ctx);
      break;
    }
  }
  return ret;
}

JSValue
minnet_url_from(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetURL* url;

  if(!(url = url_new(ctx)))
    return JS_EXCEPTION;

  url_fromvalue(url, argv[0], ctx);

  if(!url_valid(*url))
    return JS_ThrowTypeError(ctx, "Not a valid URL");

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
    return JS_EXCEPTION;

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

static const JSClassDef minnet_url_class = {
    "MinnetURL",
    .finalizer = minnet_url_finalizer,
};

static const JSCFunctionListEntry minnet_url_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("protocol", minnet_url_get, minnet_url_set, URL_PROTOCOL, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("hostname", minnet_url_get, minnet_url_set, URL_HOSTNAME, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("host", minnet_url_get, 0, URL_HOST, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("port", minnet_url_get, minnet_url_set, URL_PORT, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("pathname", minnet_url_get, minnet_url_set, URL_PATHNAME, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("path", minnet_url_get, 0, URL_PATHNAME, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("query", minnet_url_get, minnet_url_set, URL_QUERY, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("search", minnet_url_get, 0, URL_SEARCH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("hash", minnet_url_get, 0, URL_HASH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("origin", minnet_url_get, 0, URL_ORIGIN, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("tls", minnet_url_get, 0, URL_TLS, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("href", minnet_url_get, 0, URL_HREF, 0),
    JS_CFUNC_MAGIC_DEF("toString", 0, minnet_url_method, URL_TO_STRING),
    JS_CFUNC_MAGIC_DEF("toObject", 0, minnet_url_method, URL_TO_OBJECT),
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

  inspect_atom = js_symbol_static_atom(ctx, "inspect");

  if(!js_atom_is_symbol(ctx, inspect_atom)) {
    JS_FreeAtom(ctx, inspect_atom);
    inspect_atom = js_symbol_for_atom(ctx, "quickjs.inspect.custom");
  }

  if(js_atom_is_symbol(ctx, inspect_atom))
    JS_SetProperty(ctx, minnet_url_proto, inspect_atom, JS_NewCFunction(ctx, minnet_url_inspect, "inspect", 0));

  JS_FreeAtom(ctx, inspect_atom);

  if(m)
    JS_SetModuleExport(ctx, m, "URL", minnet_url_ctor);

  return 0;
}

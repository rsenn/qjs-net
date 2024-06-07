#include "js-utils.h"
#include "minnet-websocket.h"
#include "minnet-server.h"
#include "minnet-client.h"
#include "minnet-server-http.h"
#include "minnet-server-proxy.h"
#include "minnet-response.h"
#include "minnet-request.h"
#include "closure.h"
#include <list.h>
#include <quickjs-libc.h>
#include <libwebsockets.h>

#include "../libwebsockets/plugins/raw-proxy/protocol_lws_raw_proxy.c"
#include "../libwebsockets/plugins/deaddrop/protocol_lws_deaddrop.c"
#include "../libwebsockets/plugins/protocol_lws_mirror.c"
#include "minnet-plugin-broker.c"

THREAD_LOCAL JSValue minnet_server_proto, minnet_server_ctor;
THREAD_LOCAL JSClassID minnet_server_class_id = 0;

int proxy_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int ws_server_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

static JSValue minnet_server_timeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr);

static struct lws_protocols protocols[] = {
    {"ws", ws_server_callback, sizeof(struct session_data), 1024, 0, NULL, 0},
    {"http", http_server_callback, sizeof(struct session_data), 1024, 0, NULL, 0},
    /* {"defprot", lws_callback_http_dummy, sizeof(struct session_data), 1024, 0, NULL, 0},
     {"proxy-ws-raw-ws", proxy_server_callback, 0, 1024, 0, NULL, 0},
       {"proxy-ws-raw-raw", proxy_rawclient_callback, 0, 1024, 0, NULL, 0},
     {"proxy-ws", proxy_callback, 0, 1024, 0, NULL, 0},*/
    MINNET_PLUGIN_BROKER(broker),
    LWS_PLUGIN_PROTOCOL_DEADDROP,
    // LWS_PLUGIN_PROTOCOL_RAW_PROXY,
    LWS_PLUGIN_PROTOCOL_MIRROR,
    LWS_PROTOCOL_LIST_TERM,
};

static struct lws_protocols protocols2[] = {{"ws", ws_server_callback, sizeof(struct session_data), 1024, 0, NULL, 0},
                                            {"http", http_server_callback, sizeof(struct session_data), 1024, 0, NULL, 0},
                                            /* {"defprot", defprot_callback, sizeof(struct session_data), 0},
                                             {"proxy-ws-raw-ws", proxy_server_callback, 0, 1024, 0, NULL, 0},
                                              {"proxy-ws-raw-raw", proxy_rawclient_callback, 0, 1024, 0, NULL, 0},
                                           {"proxy-ws", proxy_callback, sizeof(struct session_data), 1024, 0, NULL, 0}, MINNET_PLUGIN_BROKER(broker),
                                                LWS_PLUGIN_PROTOCOL_RAW_PROXY,*/
                                            LWS_PLUGIN_PROTOCOL_MIRROR,
                                            LWS_PROTOCOL_LIST_TERM};

static const struct lws_http_mount mount = {
    /* .mount_next */ NULL,  /* linked-list "next" */
    /* .mountpoint */ "/",   /* mountpoint URL */
    /* .origin */ ".",       /* serve from dir */
    /* .def */ "index.html", /* default filename */
    /* .protocol */ NULL,
    /* .cgienv */ NULL,
    /* .extra_mimetypes */ NULL,
    /* .interpret */ NULL,
    /* .cgi_timeout */ 0,
    /* .cache_max_age */ 0,
    /* .auth_mask */ 0,
    /* .cache_reusable */ 0,
    /* .cache_revalidate */ 0,
    /* .cache_intermediaries */ 0,
    /* .origin_protocol */ LWSMPRO_FILE, /* files in a dir */
    /* .mountpoint_len */ 1,             /* char count */
    /* .basic_auth_login_file */ 0};

static const struct lws_extension extension_pmd[] = {
    {
        "permessage-deflate",
        lws_extension_callback_pm_deflate,
        "permessage-deflate"
        "; client_no_context_takeover"
        "; client_max_window_bits",
    },
    {
        NULL,
        NULL,
        NULL,
    },
};

static MinnetServer*
server_new(JSContext* ctx) {
  MinnetServer* server;

  if(!(server = js_mallocz(ctx, sizeof(MinnetServer))))
    return (void*)-1;

  server->context.error = JS_NULL;
  server->context.js = ctx;
  server->context.info = (struct lws_context_creation_info){.protocols = protocols2, .user = server};
  server->promise = (ResolveFunctions){JS_NULL, JS_NULL};

  context_add(&server->context);

  callbacks_zero(&server->on);

  return server;
}

MinnetServer*
server_dup(MinnetServer* srv) {
  ++srv->ref_count;
  return srv;
}

static BOOL
server_listen(MinnetServer* server) {
  if(!(server->context.lws = lws_create_context(&server->context.info))) {
    lwsl_err("libwebsockets init failed\n");
    return FALSE;
  }

  if(!lws_create_vhost(server->context.lws, &server->context.info)) {
    lwsl_err("Failed to create vhost\n");
    return FALSE;
  }

  JSValue timer_cb = js_function_cclosure(server->context.js, minnet_server_timeout, 4, 0, server, 0);
  uint32_t interval = lws_service_adjust_timeout(server->context.lws, 15000, 0);
  if(interval == 0)
    interval = 10;
  server->context.timer = js_timer_interval(server->context.js, timer_cb, interval);

  server->listening = TRUE;

  return TRUE;
}

void
server_free(MinnetServer* server) {
  JSContext* ctx = server->context.js;

  if(--server->ref_count == 0) {
    js_async_free(JS_GetRuntime(ctx), &server->promise);

    context_clear(&server->context);

    js_free(ctx, server);
  }
}

struct ServerMatchClosure {
  int ref_count;
  JSContext* ctx;
  const char* path;
  enum http_method method;
  JSValue this_cb, prev_cb;
  BOOL next;
};

static BOOL
server_match_all(struct ServerMatchClosure* closure) {
  return closure->path == 0 && (int)closure->method == -1;
}

static struct ServerMatchClosure*
server_match_dup(struct ServerMatchClosure* closure) {
  ++closure->ref_count;
  return closure;
}

static void
server_match_free(void* ptr) {
  struct ServerMatchClosure* closure = ptr;

  if(--closure->ref_count == 0) {
    JSContext* ctx = closure->ctx;
    JS_FreeCString(ctx, closure->path);
    JS_FreeValue(ctx, closure->this_cb);
    JS_FreeValue(ctx, closure->prev_cb);
  }
}

static JSValue
minnet_server_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  struct ServerMatchClosure* closure = opaque;

  closure->next = TRUE;
  return JS_UNDEFINED;
};

static JSValue
minnet_server_match(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  struct ServerMatchClosure* closure = opaque;
  JSValue ret = JS_UNDEFINED;
  MinnetRequest* req;
  int32_t n;
  BOOL all = server_match_all(closure);
  JSValueConst args[] = {
      argv[0],
      argv[1],
      JS_NULL,
  };

  if(!js_is_nullish(closure->prev_cb)) {
    /*if(closure->path==0 && closure->method==-1)
      args[2]= js_function_bind_argv(ctx, closure->this_cb, */

    ret = JS_Call(ctx, closure->prev_cb, JS_NULL, 2, args);
  }

  JS_ToInt32(ctx, &n, ret);
  DBG("all=%s ret=%" PRId32 " path=%s method=%s", all ? "TRUE" : "FALSE", n, closure->path, method_name(closure->method));

  if(n == 1)
    return ret;

  if((req = minnet_request_data2(ctx, argv[0]))) {
    BOOL match = request_match(req, closure->path, closure->method);

    DBG("match=%s all=%s", match ? "TRUE" : "FALSE", all ? "TRUE" : "FALSE");

    if(match) {

      //    if(all)
      args[2] = js_function_cclosure(ctx, minnet_server_next, 0, 0, server_match_dup(closure), server_match_free);

      ret = JS_Call(ctx, closure->this_cb, JS_NULL, all ? 3 : 2, args);

      if(all && !closure->next)
        ret = JS_NewInt32(ctx, 1);

      JS_FreeValue(ctx, args[2]);
    }
  }

  return ret;
}

JSValue
server_match(MinnetServer* server, const char* path, enum http_method method, JSValue callback, JSValue prev_callback) {
  JSContext* ctx = server->context.js;
  struct ServerMatchClosure* closure;

  if(!(closure = js_mallocz(ctx, sizeof(struct ServerMatchClosure))))
    return JS_EXCEPTION;

  *closure = (struct ServerMatchClosure){
      1,
      ctx,
      path,
      method,
      callback,
      prev_callback,
      FALSE,
  };

  JSValue ret = js_function_cclosure(ctx, minnet_server_match, 0, 0, closure, server_match_free);

  JS_DefinePropertyValueStr(ctx, ret, "name", JS_NewString(ctx, path != 0 ? path : "*"), 0);

  if((int)method != -1)
    JS_SetPropertyStr(ctx, ret, "method", JS_NewString(ctx, method_name(method)));

  if(JS_IsFunction(ctx, prev_callback)) {
    JS_SetPropertyStr(ctx, prev_callback, "next", ret);
    JS_SetPropertyStr(ctx, ret, "prev", JS_DupValue(ctx, prev_callback));
  }

  return ret;
}

void
server_mounts(MinnetServer* server, JSValueConst opt_mounts) {
  JSContext* ctx = server->context.js;
  struct lws_context_creation_info* info = &server->context.info;
  MinnetHttpMount** m = (MinnetHttpMount**)&info->mounts;

  *m = 0;

  if(JS_IsArray(ctx, opt_mounts)) {
    uint32_t i;
    for(i = 0;; i++) {
      MinnetHttpMount* mount;
      JSValue mountval = JS_GetPropertyUint32(ctx, opt_mounts, i);
      if(JS_IsUndefined(mountval))
        break;
      mount = mount_fromobj(ctx, mountval, 0);
      mount->extra_mimetypes = server->mimetypes;
      mount->pro = "http";
      ADD(m, mount, next);
    }
  } else if(JS_IsObject(opt_mounts)) {
    JSPropertyEnum* tmp_tab;
    uint32_t i, tmp_len = 0;
    JS_GetOwnPropertyNames(ctx, &tmp_tab, &tmp_len, opt_mounts, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK);

    for(i = 0; i < tmp_len; i++) {
      MinnetHttpMount* mount;
      JSAtom prop = tmp_tab[i].atom;
      const char* name = JS_AtomToCString(ctx, prop);
      JSValue mountval = JS_GetProperty(ctx, opt_mounts, prop);
      mount = mount_fromobj(ctx, mountval, name);
      mount->extra_mimetypes = server->mimetypes;
      mount->pro = "http";
      ADD(m, mount, next);
      JS_FreeCString(ctx, name);
    }
  }
}

void
server_certificate(struct context* context, JSValueConst options) {
  struct lws_context_creation_info* info = &context->info;
  JSContext* ctx = context->js;

  context->crt = JS_GetPropertyStr(context->js, options, "sslCert");
  context->key = JS_GetPropertyStr(context->js, options, "sslPrivateKey");
  context->ca = JS_GetPropertyStr(context->js, options, "sslCA");

  if(JS_IsString(context->crt)) {
    info->ssl_cert_filepath = js_tostring(ctx, context->crt);
#ifdef DEBUT_OUTPUT
    printf("server SSL certificate file: %s\n", info->ssl_cert_filepath);
#endif

  } else {
    info->server_ssl_cert_mem = js_toptrsize(ctx, &info->server_ssl_cert_mem_len, context->crt);
#ifdef DEBUT_OUTPUT
    printf("server SSL certificate memory: %p [%u]\n", info->server_ssl_cert_mem, info->server_ssl_cert_mem_len);
#endif
  }

  if(JS_IsString(context->key)) {
    info->ssl_private_key_filepath = js_tostring(ctx, context->key);
#ifdef DEBUT_OUTPUT
    printf("server SSL private key file: %s\n", info->ssl_private_key_filepath);
#endif

  } else {
    info->server_ssl_private_key_mem = js_toptrsize(ctx, &info->server_ssl_private_key_mem_len, context->key);
#ifdef DEBUT_OUTPUT
    printf("server SSL private key memory: %p [%u]\n", info->server_ssl_private_key_mem, info->server_ssl_private_key_mem_len);
#endif
  }

  if(JS_IsString(context->ca)) {
    info->ssl_ca_filepath = js_tostring(ctx, context->ca);
#ifdef DEBUT_OUTPUT
    printf("server SSL CA certificate file: %s\n", info->ssl_ca_filepath);
#endif

  } else {
    info->server_ssl_ca_mem = js_toptrsize(ctx, &info->server_ssl_ca_mem_len, context->ca);
#ifdef DEBUT_OUTPUT
    printf("server SSL CA certificate memory: %p [%u]\n", info->server_ssl_ca_mem, info->server_ssl_ca_mem_len);
#endif
  }
}

JSValue
minnet_server_wrap(JSContext* ctx, MinnetServer* srv) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_server_proto, minnet_server_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, server_dup(srv));
  return ret;
}

enum {
  SERVER_ONREQUEST,
  SERVER_LISTENING,
};

JSValue
minnet_server_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetServer* server;
  JSValue ret = JS_UNDEFINED;

  if(!(server = minnet_server_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SERVER_ONREQUEST: {
      ret = JS_DupValue(ctx, server->on.http.func_obj);
      break;
    }

    case SERVER_LISTENING: {
      ret = JS_NewBool(ctx, server->context.lws != 0);
      break;
    }
  }
  return ret;
}

JSValue
minnet_server_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetServer* server;
  JSValue ret = JS_UNDEFINED;

  if(!(server = minnet_server_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SERVER_ONREQUEST: {
      JS_FreeValue(ctx, server->on.http.func_obj);
      server->on.http.func_obj = JS_DupValue(ctx, value);
      server->on.http.ctx = JS_IsFunction(ctx, value) ? ctx : 0;
      break;
    }
  }
  return ret;
}

enum {
  SERVER_LISTEN,
  SERVER_GET,
  SERVER_POST,
  SERVER_USE,
  SERVER_MOUNT,
};

JSValue
minnet_server_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MinnetServer* server;
  JSValue ret = JS_UNDEFINED;

  if(!(server = minnet_server_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case SERVER_LISTEN: {
      int32_t port = -1;
      if(argc > 0)
        JS_ToInt32(ctx, &port, argv[0]);

      if(port != -1)
        server->context.info.port = port;

      if(!server_listen(server))
        ret = JS_ThrowInternalError(ctx, "server_listen failed");

      break;
    }

    case SERVER_GET:
    case SERVER_POST:
    case SERVER_USE: {
      const char* path = 0;
      int index = 0;
      enum http_method method = magic == SERVER_GET ? METHOD_GET : magic == SERVER_POST ? METHOD_POST : -1;
      JSValue fn, new_cb;

      if(JS_IsString(argv[0]) && argc > 1)
        path = JS_ToCString(ctx, argv[index++]);

      if(!JS_IsFunction(ctx, argv[index])) {
        ret = JS_ThrowTypeError(ctx, "argument %d must be a function", index + 1);
        break;
      }

      /*if(method == -1 && path == 0 && server->on.http.ctx == 0) {
        new_cb = JS_DupValue(ctx, argv[index]);
      } else */
      {
        new_cb = server_match(server, path, method, JS_DupValue(ctx, argv[index]), server->on.http.func_obj);
        path = 0;
      }

      server->on.http.func_obj = new_cb;
      server->on.http.ctx = ctx;

      if(path)
        JS_FreeCString(ctx, path);
      break;
    }

    case SERVER_MOUNT: {
      MinnetHttpMount **m = (MinnetHttpMount**)&server->context.info.mounts, *mount;
      const char* path = 0;

      while(*m)
        SKIP(m, next);

      if(JS_IsString(argv[0])) {
        path = JS_ToCString(ctx, argv[0]);
        ++argc, --argv;
      }

      if(argc == 0 || !JS_IsObject(argv[0])) {
        ret = JS_ThrowInternalError(ctx, "argument 2 must be string or an Array/Object");
        break;
      }

      if(JS_IsObject(argv[0])) {
        mount = mount_fromobj(ctx, argv[0], path);
      } else {
        const char *org = 0, *def = 0;
        char* pro = 0;

        org = JS_ToCString(ctx, argv[1]);

        if(argc > 2)
          def = JS_ToCString(ctx, argv[2]);

        if(argc > 3)
          pro = js_tostring(ctx, argv[3]);

        mount = mount_new(ctx, path, org, def, pro);

        JS_FreeCString(ctx, org);
        if(def)
          JS_FreeCString(ctx, def);
      }

      ADD(m, mount, next);

      if(path)
        JS_FreeCString(ctx, path);

      break;
    }
  }

  return ret;
}

static JSValue
minnet_server_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  return JS_UNDEFINED;
}

static JSValue
minnet_server_timeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  MinnetServer* server = ptr;
  struct TimerClosure* timer = server->context.timer;

  if(timer) {
#ifdef DEBUT_OUTPUT
    printf("timeout %" PRIu32 "\n", timer->interval);
#endif

    uint32_t new_interval;

    do {
      new_interval = lws_service_adjust_timeout(server->context.lws, 15000, 0);

      if(new_interval == 0)
        lws_service_tsi(server->context.lws, -1, 0);
    } while(new_interval == 0);

#ifdef DEBUT_OUTPUT
    printf("new_interval %" PRIu32 "\n", new_interval);
#endif

    timer->interval = new_interval;

    js_timer_restart(timer);

    return JS_FALSE;
  }
#ifdef DEBUT_OUTPUT
  printf("timeout %s %s\n", JS_ToCString(ctx, argv[0]), JS_ToCString(ctx, argv[argc - 1]));
#endif

  return JS_TRUE;
}

JSValue
minnet_server_closure(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  int argind = 0, a = 0;
  BOOL block = FALSE, is_tls = FALSE, is_h2 = TRUE, per_message_deflate = FALSE;
  MinnetServer* server;
  MinnetURL url = {0};
  JSValue ret, options;
  struct lws_context_creation_info* info;

  if((server = server_new(ctx)) == (void*)-1)
    return JS_EXCEPTION;
  if(!server)
    return JS_ThrowInternalError(ctx, "lws init failed");

  if(ptr) {
    union closure* closure = ptr;
    closure->pointer = server;
    closure->free_func = (closure_free_t*)server_free;
  }

  info = &server->context.info;

  ret = JS_NewInt32(ctx, 0);
  options = argv[0];

  if(url_fromvalue(&url, argv[argind], ctx) && argc > 1)
    ++argind;

  /* if(argc >= 2 && JS_IsString(argv[argind])) {
     const char* str;
     if((str = JS_ToCString(ctx, argv[argind]))) {
       url_parse(&url, str, ctx);
       JS_FreeCString(ctx, str);
     }
     argind++;
   }*/

  options = argv[argind];

  if(!JS_IsObject(options))
    return JS_ThrowTypeError(ctx, "argument %d must be options object", argind + 1);

  /* JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
   JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
   JSValue opt_protocol = JS_GetPropertyStr(ctx, options, "protocol");*/
  JSValue opt_tls = JS_GetPropertyStr(ctx, options, "tls");
  JSValue opt_on_pong = JS_GetPropertyStr(ctx, options, "onPong");
  JSValue opt_on_close = JS_GetPropertyStr(ctx, options, "onClose");
  JSValue opt_on_connect = JS_GetPropertyStr(ctx, options, "onConnect");
  JSValue opt_on_message = JS_GetPropertyStr(ctx, options, "onMessage");
  JSValue opt_on_fd = JS_GetPropertyStr(ctx, options, "onFd");
  JSValue opt_on_http = JS_GetPropertyStr(ctx, options, "onRequest");
  JSValue opt_on_read = JS_GetPropertyStr(ctx, options, "onRead");
  JSValue opt_on_post = JS_GetPropertyStr(ctx, options, "onPost");
  JSValue opt_mounts = JS_GetPropertyStr(ctx, options, "mounts");
  JSValue opt_mimetypes = JS_GetPropertyStr(ctx, options, "mimetypes");
  JSValue opt_error_document = JS_GetPropertyStr(ctx, options, "errorDocument");
  JSValue opt_options = JS_GetPropertyStr(ctx, options, "options");

  if(!JS_IsFunction(ctx, opt_on_fd))
    opt_on_fd = minnet_default_fd_callback(ctx);

  if(!JS_IsUndefined(opt_tls)) {

    is_tls = JS_ToBool(ctx, opt_tls);
#ifdef DEBUT_OUTPUT
    printf("is_tls = %d\n", is_tls);
#endif
  }

  /*if(!JS_IsUndefined(opt_port)) {
    int32_t port;
    JS_ToInt32(ctx, &port, opt_port);
    url.port = port;
  }

  if(!JS_IsUndefined(opt_host))
    js_replace_string(ctx, opt_host, &url.host);

  if(JS_IsString(opt_protocol)) {
    const char* protocol;

    if((protocol = JS_ToCString(ctx, opt_protocol))) {
      url_set_protocol(&url, protocol);
      JS_FreeCString(ctx, protocol);
    }
  }
*/
  BOOL_OPTION(opt_h2, "h2", is_h2);
  BOOL_OPTION(opt_pmd, "permessageDeflate", per_message_deflate);

  GETCB(opt_on_pong, server->on.pong)
  GETCB(opt_on_close, server->on.close)
  GETCB(opt_on_connect, server->on.connect)
  GETCB(opt_on_message, server->on.message)
  GETCB(opt_on_fd, server->on.fd)
  GETCB(opt_on_http, server->on.http)
  GETCB(opt_on_read, server->on.read)
  GETCB(opt_on_post, server->on.post)

  for(size_t i = 0; i < countof(protocols); i++)
    protocols[i].user = ctx;

  info->protocols = protocols2;

  info->mounts = &mount;
  info->vhost_name = url_format((MinnetURL){.host = url.host, .port = url.port}, ctx);

  if(JS_IsString(opt_error_document))
    info->error_document_404 = js_tostring(ctx, opt_error_document);
  else
    info->error_document_404 = js_strdup(ctx, "/404.html");

  info->port = url.port;
  info->timeout_secs = 0;
  info->options = 0;

  if(per_message_deflate)
    info->extensions = extension_pmd;

  // client_certificate(&server->context, options);

  info->options |= LWS_SERVER_OPTION_PEER_CERT_NOT_REQUIRED;
  info->options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info->options |= LWS_SERVER_OPTION_DISABLE_IPV6;
  info->options |= LWS_SERVER_OPTION_IGNORE_MISSING_CERT;

  // info->options |= LWS_SERVER_OPTION_CREATE_VHOST_SSL_CTX;

  info->options |= LWS_SERVER_OPTION_REQUIRE_VALID_OPENSSL_CLIENT_CERT;

  if(is_tls) {
    server_certificate(&server->context, options);

    // info->options |= LWS_SERVER_OPTION_REDIRECT_HTTP_TO_HTTPS;
    info->options |= LWS_SERVER_OPTION_ALLOW_HTTP_ON_HTTPS_LISTENER;
    info->options |= LWS_SERVER_OPTION_ALLOW_NON_SSL_ON_SSL_PORT;
  }
  client_certificate(&server->context, options);

  if(is_h2) {
    info->options |= LWS_SERVER_OPTION_H2_JUST_FIX_WINDOW_UPDATE_OVERFLOW;
    info->options |= LWS_SERVER_OPTION_VH_H2_HALF_CLOSED_LONG_POLL;
  }
  // info->options |= LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;

  if(JS_IsArray(ctx, opt_mimetypes)) {
    MinnetVhostOptions *vopts, **vop = &server->mimetypes;
    uint32_t i;
    for(i = 0;; i++) {
      JSValue mimetype = JS_GetPropertyUint32(ctx, opt_mimetypes, i);
      if(JS_IsUndefined(mimetype))
        break;
      vopts = vhost_options_new(ctx, mimetype);
      ADD(vop, vopts, next);
    }
  }

  /*{
    MinnetVhostOptions* pvo;

    for(pvo = server->mimetypes; pvo; pvo = pvo->next) {
      #ifdef DEBUT_OUTPUT
printf("pvo mimetype %s %s\n", pvo->name, pvo->value);
#endif

    }
  }*/

  MinnetVhostOptions *vhopt = 0, **vhptr = &vhopt;

  ADD(vhptr, vhost_options_create(ctx, "lws-deaddrop", ""), next);
  ADD(vhptr, vhost_options_create(ctx, "lws-mirror-protocol", ""), next);
  ADD(vhptr, vhost_options_create(ctx, "raw-proxy", ""), next);

  info->pvo = &vhopt->lws;

  if(!JS_IsUndefined(opt_options)) {

    vhopt->options = vhost_options_fromobj(ctx, opt_options);

    fprintf(stderr, "vhost options:\n");
    vhost_options_dump(vhopt->options);
  }

  server_mounts(server, opt_mounts);

  if(server->context.info.port > 0)
    if(!server_listen(server))
      return JS_ThrowInternalError(ctx, "libwebsockets init failed");

  ret = minnet_server_wrap(ctx, server);

  if(!block)
    return ret;

  while(a >= 0) {
    if(!JS_IsNull(server->context.error)) {
      ret = JS_Throw(ctx, server->context.error);
      break;
    }

    if(server->on.fd.ctx)
      js_std_loop(ctx);
    else
      a = lws_service(server->context.lws, 20);
  }

  // lws_context_destroy(server->context.lws);

  if(server->mimetypes)
    vhost_options_free_list(ctx, server->mimetypes);

  if(info->mounts) {
    const MinnetHttpMount *mount, *next;

    for(mount = (MinnetHttpMount*)info->mounts; mount; mount = next) {
      next = (MinnetHttpMount*)mount->lws.mount_next;
      mount_free(ctx, mount);
    }
  }

  if(info->server_ssl_ca_mem)
    js_clear(ctx, &info->server_ssl_ca_mem);
  if(info->server_ssl_cert_mem)
    js_clear(ctx, &info->server_ssl_cert_mem);
  if(info->server_ssl_private_key_mem)
    js_clear(ctx, &info->server_ssl_private_key_mem);
  if(info->ssl_ca_filepath)
    js_clear(ctx, &info->ssl_ca_filepath);
  if(info->ssl_cert_filepath)
    js_clear(ctx, &info->ssl_cert_filepath);
  if(info->ssl_private_key_filepath)
    js_clear(ctx, &info->ssl_private_key_filepath);

  /*js_buffer_free(&server->context.key, JS_GetRuntime(ctx));
  js_buffer_free(&server->context.crt, JS_GetRuntime(ctx));
  js_buffer_free(&server->context.ca, JS_GetRuntime(ctx));*/

  /* if(info->ssl_cert_filepath)
     JS_FreeCString(ctx, info->ssl_cert_filepath);

   if(info->ssl_private_key_filepath)
     JS_FreeCString(ctx, info->ssl_private_key_filepath);

   js_free(ctx, (void*)info->vhost_name);
 */
  FREECB(server->on.pong)
  FREECB(server->on.close)
  FREECB(server->on.connect)
  FREECB(server->on.message)
  FREECB(server->on.fd)
  FREECB(server->on.http)

  return ret;
}

JSValue
minnet_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  union closure* closure;
  JSValue ret;

  if(!(closure = closure_new(ctx)))
    return JS_EXCEPTION;

  ret = minnet_server_closure(ctx, this_val, argc, argv, 0, closure);

  if(js_is_promise(ctx, ret)) {
    JSValue func[2], tmp;

    func[0] = js_function_cclosure(ctx, &minnet_server_handler, 1, ON_RESOLVE, closure_dup(closure), closure_free);
    func[1] = js_function_cclosure(ctx, &minnet_server_handler, 1, ON_REJECT, closure_dup(closure), closure_free);

    tmp = js_invoke(ctx, ret, "then", 1, &func[0]);
    JS_FreeValue(ctx, ret);
    ret = tmp;

    tmp = js_invoke(ctx, ret, "catch", 1, &func[1]);
    JS_FreeValue(ctx, ret);
    ret = tmp;

    JS_FreeValue(ctx, func[0]);
    JS_FreeValue(ctx, func[1]);
  } else if(closure->pointer) {
    ret = minnet_server_wrap(ctx, closure->pointer);
  }

  closure_free(closure);

  return ret;
}

static const JSCFunctionListEntry minnet_server_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("listen", 0, minnet_server_method, SERVER_LISTEN),
    JS_CFUNC_MAGIC_DEF("get", 2, minnet_server_method, SERVER_GET),
    JS_CFUNC_MAGIC_DEF("post", 2, minnet_server_method, SERVER_POST),
    JS_CFUNC_MAGIC_DEF("use", 2, minnet_server_method, SERVER_USE),
    JS_CFUNC_MAGIC_DEF("mount", 1, minnet_server_method, SERVER_MOUNT),
    JS_CGETSET_MAGIC_DEF("onrequest", minnet_server_get, minnet_server_set, SERVER_ONREQUEST),
    JS_CGETSET_MAGIC_FLAGS_DEF("listening", minnet_server_get, 0, SERVER_LISTENING, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetServer", JS_PROP_CONFIGURABLE),
};

static void
minnet_server_finalizer(JSRuntime* rt, JSValue val) {
  MinnetServer* srv;

  if((srv = minnet_server_data(val))) {

    /*ptr->free_func(ptr->opaque, rt);
    ptr->server = 0;
    js_free_rt(rt, ptr);*/
  }
}

static const JSClassDef minnet_server_class = {
    .class_name = "MinnetServer",
    .finalizer = minnet_server_finalizer,
};

int
minnet_server_init(JSContext* ctx, JSModuleDef* m) {
  // Add class Server
  JS_NewClassID(&minnet_server_class_id);

  JS_NewClass(JS_GetRuntime(ctx), minnet_server_class_id, &minnet_server_class);
  minnet_server_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_server_proto, minnet_server_proto_funcs, countof(minnet_server_proto_funcs));
  JS_SetClassProto(ctx, minnet_server_class_id, minnet_server_proto);

  minnet_server_ctor = JS_NewObject(ctx);
  JS_SetConstructor(ctx, minnet_server_ctor, minnet_server_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Server", minnet_server_ctor);

  return 0;
}

/*int
defprot_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetServer* server = lws_context_user(lws_get_context(wsi));

  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: {
      return 0;
    }

    case LWS_CALLBACK_ADD_POLL_FD: {
      struct lws_pollargs* args = in;
      if(server->on.fd.ctx) {
        JSValue argv[3] = {JS_NewInt32(server->on.fd.ctx, args->fd)};
        minnet_io_handlers(server->on.fd.ctx, wsi, *args, &argv[1]);
        server_exception(server, callback_emit(&server->on.fd, 3, argv));
        JS_FreeValue(server->on.fd.ctx, argv[0]);
        JS_FreeValue(server->on.fd.ctx, argv[1]);
        JS_FreeValue(server->on.fd.ctx, argv[2]);
      }
      return 0;
    }

    case LWS_CALLBACK_DEL_POLL_FD: {
      struct lws_pollargs* args = in;
      if(server->on.fd.ctx) {
        JSValue argv[3] = {
            JS_NewInt32(server->on.fd.ctx, args->fd),
        };
        minnet_io_handlers(server->on.fd.ctx, wsi, *args, &argv[1]);
        server_exception(server, callback_emit(&server->on.fd, 3, argv));
        JS_FreeValue(server->on.fd.ctx, argv[0]);
        JS_FreeValue(server->on.fd.ctx, argv[1]);
        JS_FreeValue(server->on.fd.ctx, argv[2]);
      }
      return 0;
    }

    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      struct lws_pollargs* args = in;
      if(server->on.fd.ctx) {
        if(args->events != args->prev_events) {
          JSValue argv[3] = {JS_NewInt32(server->on.fd.ctx, args->fd)};
          minnet_io_handlers(server->on.fd.ctx, wsi, *args, &argv[1]);
          server_exception(server, callback_emit(&server->on.fd, 3, argv));
          JS_FreeValue(server->on.fd.ctx, argv[0]);
          JS_FreeValue(server->on.fd.ctx, argv[1]);
          JS_FreeValue(server->on.fd.ctx, argv[2]);
        }
      }
      return 0;
    }

    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
    case LWS_CALLBACK_GET_THREAD_ID: {
      return 0;
    }
    default: {
      break;
    }
  }

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}*/

#include "jsutils.h"
#include "minnet-websocket.h"
#include "minnet-server.h"
#include "minnet-server-http.h"
#include "minnet-server-proxy.h"
#include "minnet-response.h"
#include "minnet-request.h"
#include <list.h>
#include <quickjs-libc.h>
#include <libwebsockets.h>

#include "libwebsockets/plugins/raw-proxy/protocol_lws_raw_proxy.c"
#include "minnet-plugin-broker.c"

int proxy_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

static struct lws_protocols protocols[] = {
    {"ws", ws_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    {"http", http_server_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    {"defprot", lws_callback_http_dummy, sizeof(MinnetSession), 1024, 0, NULL, 0},
    {"proxy-ws-raw-ws", callback_proxy_ws_server, 0, 1024, 0, NULL, 0},
    {"proxy-ws-raw-raw", callback_proxy_raw_client, 0, 1024, 0, NULL, 0},
    // {"proxy-ws", proxy_callback, 0, 1024, 0, NULL, 0},
    MINNET_PLUGIN_BROKER(broker),
    LWS_PLUGIN_PROTOCOL_RAW_PROXY,
    {0},
};

static struct lws_protocols protocols2[] = {
    {"ws", ws_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    {"http", http_server_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    {"defprot", defprot_callback, sizeof(MinnetSession), 0},
    {"proxy-ws-raw-ws", callback_proxy_ws_server, 0, 1024, 0, NULL, 0},
    {"proxy-ws-raw-raw", callback_proxy_raw_client, 0, 1024, 0, NULL, 0},
    //  {"proxy-ws", proxy_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    MINNET_PLUGIN_BROKER(broker),
    LWS_PLUGIN_PROTOCOL_RAW_PROXY,
    {0, 0},
};

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
    /* .basic_auth_login_file */ NULL,
};

static const struct lws_extension extensions[] = {
    {"permessage-deflate",
     lws_extension_callback_pm_deflate,
     "permessage-deflate"
     "; client_no_context_takeover"
     "; client_max_window_bits"},
    {NULL, NULL, NULL /* terminator */},
};

static MinnetServer*
server_new(JSContext* ctx) {
  MinnetServer* server;

  if(!(server = js_mallocz(ctx, sizeof(MinnetServer))))
    return (void*)-1;

  server->context.error = JS_NULL;
  server->context.js = ctx;
  server->context.info = (struct lws_context_creation_info){.protocols = protocols2, .user = server};

  return server;
}

static BOOL
server_init(MinnetServer* server) {
  if(!(server->context.lws = lws_create_context(&server->context.info))) {
    lwsl_err("libwebsockets init failed\n");
    return FALSE;
  }

  if(!lws_create_vhost(server->context.lws, &server->context.info)) {
    lwsl_err("Failed to create vhost\n");
    return FALSE;
  }

  return TRUE;
}

void
server_free(MinnetServer* server) {
  JSContext* ctx = server->context.js;

  if(--server->ref_count == 0) {
    js_promise_free(ctx, &server->promise);

    context_clear(&server->context);

    js_free(ctx, server);
  }
}
void
server_certificate(MinnetContext* context, JSValueConst options) {
  struct lws_context_creation_info* info = &context->info;
  JSContext* ctx = context->js;

  context->crt = JS_GetPropertyStr(context->js, options, "sslCert");
  context->key = JS_GetPropertyStr(context->js, options, "sslPrivateKey");
  context->ca = JS_GetPropertyStr(context->js, options, "sslCA");

  if(JS_IsString(context->crt)) {
    info->ssl_cert_filepath = js_tostring(ctx, context->crt);
    printf("server SSL certificate file: %s\n", info->ssl_cert_filepath);
  } else {
    info->server_ssl_cert_mem = js_toptrsize(ctx, &info->server_ssl_cert_mem_len, context->crt);
    printf("server SSL certificate memory: %p [%u]\n", info->server_ssl_cert_mem, info->server_ssl_cert_mem_len);
  }

  if(JS_IsString(context->key)) {
    info->ssl_private_key_filepath = js_tostring(ctx, context->key);
    printf("server SSL private key file: %s\n", info->ssl_private_key_filepath);
  } else {
    info->server_ssl_private_key_mem = js_toptrsize(ctx, &info->server_ssl_private_key_mem_len, context->key);
    printf("server SSL private key memory: %p [%u]\n", info->server_ssl_private_key_mem, info->server_ssl_private_key_mem_len);
  }

  if(JS_IsString(context->ca)) {
    info->ssl_ca_filepath = js_tostring(ctx, context->ca);
    printf("server SSL CA certificate file: %s\n", info->ssl_ca_filepath);
  } else {
    info->server_ssl_ca_mem = js_toptrsize(ctx, &info->server_ssl_ca_mem_len, context->ca);
    printf("server SSL CA certificate memory: %p [%u]\n", info->server_ssl_ca_mem, info->server_ssl_ca_mem_len);
  }
}

static JSValue
minnet_server_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  return JS_UNDEFINED;
}

JSValue
minnet_server_closure(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  int argind = 0, a = 0;
  BOOL block = TRUE, is_tls = FALSE, is_h2 = TRUE;
  MinnetServer* server;
  MinnetURL url = {0};
  JSValue ret, options;
  struct lws_context_creation_info* info;

  if((server = server_new(ctx)) == (void*)-1)
    return JS_ThrowOutOfMemory(ctx);
  if(!server)
    return JS_ThrowInternalError(ctx, "lws init failed");

  if(ptr) {
    ((MinnetClosure*)ptr)->server = server;
    ((MinnetClosure*)ptr)->free_func = &server_free;
  }

  info = &server->context.info;

  ret = JS_NewInt32(ctx, 0);
  options = argv[0];

  if(argc >= 2 && JS_IsString(argv[argind])) {
    const char* str;
    if((str = JS_ToCString(ctx, argv[argind]))) {
      url_parse(&url, str, ctx);
      JS_FreeCString(ctx, str);
    }
    argind++;
  }

  options = argv[argind];

  if(!JS_IsObject(options))
    return JS_ThrowTypeError(ctx, "argument %d must be options object", argind + 1);

  JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
  JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
  JSValue opt_protocol = JS_GetPropertyStr(ctx, options, "protocol");
  JSValue opt_tls = JS_GetPropertyStr(ctx, options, "tls");
  JSValue opt_on_pong = JS_GetPropertyStr(ctx, options, "onPong");
  JSValue opt_on_close = JS_GetPropertyStr(ctx, options, "onClose");
  JSValue opt_on_connect = JS_GetPropertyStr(ctx, options, "onConnect");
  JSValue opt_on_message = JS_GetPropertyStr(ctx, options, "onMessage");
  JSValue opt_on_fd = JS_GetPropertyStr(ctx, options, "onFd");
  JSValue opt_on_http = JS_GetPropertyStr(ctx, options, "onHttp");
  JSValue opt_mounts = JS_GetPropertyStr(ctx, options, "mounts");
  JSValue opt_mimetypes = JS_GetPropertyStr(ctx, options, "mimetypes");

  if(!JS_IsUndefined(opt_tls)) {

    is_tls = JS_ToBool(ctx, opt_tls);
    printf("is_tls = %d\n", is_tls);
  }

  if(!JS_IsUndefined(opt_port)) {
    int32_t port;
    JS_ToInt32(ctx, &port, opt_port);
    url.port = port;
  }

  if(JS_IsString(opt_host))
    js_replace_string(ctx, opt_host, &url.host);
  if(JS_IsString(opt_protocol)) {
    const char* protocol;

    if((protocol = JS_ToCString(ctx, opt_protocol))) {
      url_set_protocol(&url, protocol);
      JS_FreeCString(ctx, protocol);
    }
  }

  JSValue opt_block = JS_GetPropertyStr(ctx, options, "block");
  if(!JS_IsUndefined(opt_block))
    block = JS_ToBool(ctx, opt_block);
  JS_FreeValue(ctx, opt_block);

  JSValue opt_h2 = JS_GetPropertyStr(ctx, options, "h2");
  if(!JS_IsUndefined(opt_h2))
    is_h2 = JS_ToBool(ctx, opt_h2);
  JS_FreeValue(ctx, opt_h2);

  GETCB(opt_on_pong, server->cb.pong)
  GETCB(opt_on_close, server->cb.close)
  GETCB(opt_on_connect, server->cb.connect)
  GETCB(opt_on_message, server->cb.message)
  GETCB(opt_on_fd, server->cb.fd)
  GETCB(opt_on_http, server->cb.http)

  for(int i = 0; i < countof(protocols); i++) protocols[i].user = ctx;

  info->protocols = protocols2;
  info->mounts = &mount;
  info->vhost_name = url_format((MinnetURL){.host = url.host, .port = url.port}, ctx);
  info->error_document_404 = "/404.html";
  info->port = url.port;

  if(is_tls) {
    server_certificate(&server->context, options);

    info->options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info->options |= LWS_SERVER_OPTION_PEER_CERT_NOT_REQUIRED;
    // info->options |= LWS_SERVER_OPTION_REDIRECT_HTTP_TO_HTTPS;
    info->options |= LWS_SERVER_OPTION_ALLOW_HTTP_ON_HTTPS_LISTENER;
    info->options |= LWS_SERVER_OPTION_ALLOW_NON_SSL_ON_SSL_PORT;
  }

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

  {
    MinnetVhostOptions* pvo;

    for(pvo = server->mimetypes; pvo; pvo = pvo->next) {
      // printf("pvo mimetype %s %s\n", pvo->name, pvo->value);
    }
  }

  info->mounts = 0;
  {
    MinnetHttpMount** m = (MinnetHttpMount**)&info->mounts;

    if(JS_IsArray(ctx, opt_mounts)) {
      uint32_t i;
      for(i = 0;; i++) {
        MinnetHttpMount* mount;
        JSValue mountval = JS_GetPropertyUint32(ctx, opt_mounts, i);
        if(JS_IsUndefined(mountval))
          break;
        mount = mount_new(ctx, mountval, 0);
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
        mount = mount_new(ctx, mountval, name);
        mount->extra_mimetypes = server->mimetypes;
        mount->pro = "http";
        ADD(m, mount, next);
        JS_FreeCString(ctx, name);
      }
    }
  }

  if(!server_init(server))
    return JS_ThrowInternalError(ctx, "libwebsockets init failed");

  if(!block)
    return ret;

  lws_service_adjust_timeout(server->context.lws, 1, 0);

  while(a >= 0) {
    if(!JS_IsNull(server->context.error)) {
      ret = JS_Throw(ctx, server->context.error);
      break;
    }

    if(server->cb.fd.ctx)
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

  /*js_buffer_free(&server->context.key, ctx);
  js_buffer_free(&server->context.crt, ctx);
  js_buffer_free(&server->context.ca, ctx);*/

  /* if(info->ssl_cert_filepath)
     JS_FreeCString(ctx, info->ssl_cert_filepath);

   if(info->ssl_private_key_filepath)
     JS_FreeCString(ctx, info->ssl_private_key_filepath);

   js_free(ctx, (void*)info->vhost_name);
 */
  FREECB(server->cb.pong)
  FREECB(server->cb.close)
  FREECB(server->cb.connect)
  FREECB(server->cb.message)
  FREECB(server->cb.fd)
  FREECB(server->cb.http)

  return ret;
}

JSValue
minnet_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct closure* closure;
  JSValue ret;

  if(!(closure = closure_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  ret = minnet_server_closure(ctx, this_val, argc, argv, 0, closure);

  if(js_is_promise(ctx, ret)) {
    JSValue func[2], tmp;

    func[0] = JS_NewCClosure(ctx, &minnet_server_handler, 1, ON_RESOLVE, closure_dup(closure), closure_free);
    func[1] = JS_NewCClosure(ctx, &minnet_server_handler, 1, ON_REJECT, closure_dup(closure), closure_free);

    tmp = js_invoke(ctx, ret, "then", 1, &func[0]);
    JS_FreeValue(ctx, ret);
    ret = tmp;

    tmp = js_invoke(ctx, ret, "catch", 1, &func[1]);
    JS_FreeValue(ctx, ret);
    ret = tmp;

    JS_FreeValue(ctx, func[0]);
    JS_FreeValue(ctx, func[1]);
  }

  closure_free(closure);

  return ret;
}

int
defprot_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetSession* session = user;
  MinnetServer* server = /*session ? session->server :*/ lws_context_user(lws_get_context(wsi));
  JSContext* ctx = server->context.js;

  // if(!lws_is_poll_callback(reason)) printf("defprot_callback %s %p %p %zu\n", lws_callback_name(reason), user, in, len);

  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: {
      return 0;
    }
    case LWS_CALLBACK_ADD_POLL_FD: {
      struct lws_pollargs* args = in;
      if(server->cb.fd.ctx) {
        JSValue argv[3] = {JS_NewInt32(server->cb.fd.ctx, args->fd)};
        minnet_handlers(server->cb.fd.ctx, wsi, *args, &argv[1]);
        server_exception(server, minnet_emit(&server->cb.fd, 3, argv));
        JS_FreeValue(server->cb.fd.ctx, argv[0]);
        JS_FreeValue(server->cb.fd.ctx, argv[1]);
        JS_FreeValue(server->cb.fd.ctx, argv[2]);
      }
      return 0;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {
      struct lws_pollargs* args = in;
      if(server->cb.fd.ctx) {
        JSValue argv[3] = {
            JS_NewInt32(server->cb.fd.ctx, args->fd),
        };
        minnet_handlers(server->cb.fd.ctx, wsi, *args, &argv[1]);
        server_exception(server, minnet_emit(&server->cb.fd, 3, argv));
        JS_FreeValue(server->cb.fd.ctx, argv[0]);
        JS_FreeValue(server->cb.fd.ctx, argv[1]);
        JS_FreeValue(server->cb.fd.ctx, argv[2]);
      }
      return 0;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      struct lws_pollargs* args = in;
      if(server->cb.fd.ctx) {
        if(args->events != args->prev_events) {
          JSValue argv[3] = {JS_NewInt32(server->cb.fd.ctx, args->fd)};
          minnet_handlers(server->cb.fd.ctx, wsi, *args, &argv[1]);
          server_exception(server, minnet_emit(&server->cb.fd, 3, argv));
          JS_FreeValue(server->cb.fd.ctx, argv[0]);
          JS_FreeValue(server->cb.fd.ctx, argv[1]);
          JS_FreeValue(server->cb.fd.ctx, argv[2]);
        }
      }
      return 0;
    }
  }

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

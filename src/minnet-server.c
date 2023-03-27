#include "jsutils.h"
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

int proxy_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

static struct lws_protocols protocols[] = {
    {"ws", ws_server_callback, sizeof(struct session_data), 1024, 0, NULL, 0},
    {"http", http_server_callback, sizeof(struct session_data), 1024, 0, NULL, 0},
    {"defprot", lws_callback_http_dummy, sizeof(struct session_data), 1024, 0, NULL, 0},
    /*  {"proxy-ws-raw-ws", proxy_server_callback, 0, 1024, 0, NULL, 0},
      {"proxy-ws-raw-raw", proxy_rawclient_callback, 0, 1024, 0, NULL, 0},
  */ // {"proxy-ws", proxy_callback, 0, 1024, 0, NULL, 0},
    MINNET_PLUGIN_BROKER(broker),
    LWS_PLUGIN_PROTOCOL_DEADDROP,
    // LWS_PLUGIN_PROTOCOL_RAW_PROXY,
    LWS_PLUGIN_PROTOCOL_MIRROR,
    {0},
};

static struct lws_protocols protocols2[] = {
    {"ws", ws_server_callback, sizeof(struct session_data), 1024, 0, NULL, 0},
    {"http", http_server_callback, sizeof(struct session_data), 1024, 0, NULL, 0},
    {"defprot", defprot_callback, sizeof(struct session_data), 0},
    /* {"proxy-ws-raw-ws", proxy_server_callback, 0, 1024, 0, NULL, 0},
     {"proxy-ws-raw-raw", proxy_rawclient_callback, 0, 1024, 0, NULL, 0},
 */ //  {"proxy-ws", proxy_callback, sizeof(struct session_data), 1024, 0, NULL, 0},
    MINNET_PLUGIN_BROKER(broker),
    //  LWS_PLUGIN_PROTOCOL_RAW_PROXY,
    LWS_PLUGIN_PROTOCOL_MIRROR,
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

  context_add(&server->context);

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

void
server_certificate(struct context* context, JSValueConst options) {
  struct lws_context_creation_info* info = &context->info;
  JSContext* ctx = context->js;

  context->crt = JS_GetPropertyStr(context->js, options, "sslCert");
  context->key = JS_GetPropertyStr(context->js, options, "sslPrivateKey");
  context->ca = JS_GetPropertyStr(context->js, options, "sslCA");

  if(JS_IsString(context->crt)) {
    info->ssl_cert_filepath = js_tostring(ctx, context->crt);
    DEBUG("server SSL certificate file: %s\n", info->ssl_cert_filepath);
  } else {
    info->server_ssl_cert_mem = js_toptrsize(ctx, &info->server_ssl_cert_mem_len, context->crt);
    DEBUG("server SSL certificate memory: %p [%u]\n", info->server_ssl_cert_mem, info->server_ssl_cert_mem_len);
  }

  if(JS_IsString(context->key)) {
    info->ssl_private_key_filepath = js_tostring(ctx, context->key);
    DEBUG("server SSL private key file: %s\n", info->ssl_private_key_filepath);
  } else {
    info->server_ssl_private_key_mem = js_toptrsize(ctx, &info->server_ssl_private_key_mem_len, context->key);
    DEBUG("server SSL private key memory: %p [%u]\n", info->server_ssl_private_key_mem, info->server_ssl_private_key_mem_len);
  }

  if(JS_IsString(context->ca)) {
    info->ssl_ca_filepath = js_tostring(ctx, context->ca);
    DEBUG("server SSL CA certificate file: %s\n", info->ssl_ca_filepath);
  } else {
    info->server_ssl_ca_mem = js_toptrsize(ctx, &info->server_ssl_ca_mem_len, context->ca);
    DEBUG("server SSL CA certificate memory: %p [%u]\n", info->server_ssl_ca_mem, info->server_ssl_ca_mem_len);
  }
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
    // DEBUG("timeout %" PRIu32 "\n", timer->interval);
    uint32_t new_interval;

    do {
      new_interval = lws_service_adjust_timeout(server->context.lws, 15000, 0);

      if(new_interval == 0)
        lws_service_tsi(server->context.lws, -1, 0);
    } while(new_interval == 0);

    // DEBUG("new_interval %" PRIu32 "\n", new_interval);
    timer->interval = new_interval;

    js_timer_restart(timer);

    return JS_FALSE;
  }
  // DEBUG("timeout %s %s\n", JS_ToCString(ctx, argv[0]), JS_ToCString(ctx, argv[argc - 1]));

  return JS_TRUE;
}

JSValue
minnet_server_closure(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  int argind = 0, a = 0;
  BOOL block = TRUE, is_tls = FALSE, is_h2 = TRUE, per_message_deflate = FALSE;
  MinnetServer* server;
  MinnetURL url = {0};
  JSValue ret, options;
  struct lws_context_creation_info* info;

  if((server = server_new(ctx)) == (void*)-1)
    return JS_ThrowOutOfMemory(ctx);
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
  JSValue opt_on_read = JS_GetPropertyStr(ctx, options, "onRead");
  JSValue opt_on_post = JS_GetPropertyStr(ctx, options, "onPost");
  JSValue opt_mounts = JS_GetPropertyStr(ctx, options, "mounts");
  JSValue opt_mimetypes = JS_GetPropertyStr(ctx, options, "mimetypes");
  JSValue opt_error_document = JS_GetPropertyStr(ctx, options, "errorDocument");
  JSValue opt_options = JS_GetPropertyStr(ctx, options, "options");

  if(!JS_IsUndefined(opt_tls)) {

    is_tls = JS_ToBool(ctx, opt_tls);
    DEBUG("is_tls = %d\n", is_tls);
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

  BOOL_OPTION(opt_block, "block", block);
  BOOL_OPTION(opt_h2, "h2", is_h2);
  BOOL_OPTION(opt_pmd, "permessageDeflate", per_message_deflate);

  GETCB(opt_on_pong, server->cb.pong)
  GETCB(opt_on_close, server->cb.close)
  GETCB(opt_on_connect, server->cb.connect)
  GETCB(opt_on_message, server->cb.message)
  GETCB(opt_on_fd, server->cb.fd)
  GETCB(opt_on_http, server->cb.http)
  GETCB(opt_on_read, server->cb.read)
  GETCB(opt_on_post, server->cb.post)

  for(int i = 0; i < countof(protocols); i++) protocols[i].user = ctx;

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
      DEBUG("pvo mimetype %s %s\n", pvo->name, pvo->value);
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

  if(!server_init(server))
    return JS_ThrowInternalError(ctx, "libwebsockets init failed");

  JSValue timer_cb = js_function_cclosure(ctx, minnet_server_timeout, 4, 0, server, 0);
  uint32_t interval = lws_service_adjust_timeout(server->context.lws, 15000, 0);
  if(interval == 0)
    interval = 10;
  server->context.timer = js_timer_interval(ctx, timer_cb, interval);

  if(!block)
    return ret;

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
  union closure* closure;
  JSValue ret;

  if(!(closure = closure_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

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
  }

  closure_free(closure);

  return ret;
}

int
defprot_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetServer* server = /*session ? session->server :*/ lws_context_user(lws_get_context(wsi));

  // if(!lws_reason_poll(reason)) printf("defprot_callback %s %p %p %zu\n", lws_callback_name(reason), user, in, len);

  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: {
      return 0;
    }
    case LWS_CALLBACK_ADD_POLL_FD: {
      struct lws_pollargs* args = in;
      if(server->cb.fd.ctx) {
        JSValue argv[3] = {JS_NewInt32(server->cb.fd.ctx, args->fd)};
        minnet_io_handlers(server->cb.fd.ctx, wsi, *args, &argv[1]);
        server_exception(server, callback_emit(&server->cb.fd, 3, argv));
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
        minnet_io_handlers(server->cb.fd.ctx, wsi, *args, &argv[1]);
        server_exception(server, callback_emit(&server->cb.fd, 3, argv));
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
          minnet_io_handlers(server->cb.fd.ctx, wsi, *args, &argv[1]);
          server_exception(server, callback_emit(&server->cb.fd, 3, argv));
          JS_FreeValue(server->cb.fd.ctx, argv[0]);
          JS_FreeValue(server->cb.fd.ctx, argv[1]);
          JS_FreeValue(server->cb.fd.ctx, argv[2]);
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
}

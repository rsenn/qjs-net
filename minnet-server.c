#include "jsutils.h"
#include "minnet-websocket.h"
#include "minnet-server.h"
#include "minnet-server-http.h"
#include "minnet-response.h"
#include "minnet-request.h"
#include "minnet-url.h"
#include <list.h>
#include <quickjs-libc.h>
#include <libwebsockets.h>

//#include "libwebsockets/plugins/raw-proxy/protocol_lws_raw_proxy.c"
#include "minnet-plugin-broker.c"

// THREAD_LOCAL MinnetServer minnet_server = {0};

int proxy_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
/*int raw_client_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int ws_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int defprot_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);*/
// int http_server_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

static struct lws_protocols protocols[] = {
    {"ws", ws_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    {"defprot", lws_callback_http_dummy, sizeof(MinnetSession), 1024, 0, NULL, 0},
    {"http", http_server_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    // {"proxy-ws", proxy_callback, 0, 1024, 0, NULL, 0},
    MINNET_PLUGIN_BROKER(broker),
    {0},
};

static struct lws_protocols protocols2[] = {
    {"ws", ws_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    {"defprot", defprot_callback, sizeof(MinnetSession), 0},
    {"http", http_server_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    //  {"proxy-ws", proxy_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    MINNET_PLUGIN_BROKER(broker),
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

static const struct lws_extension extensions[] = {{"permessage-deflate",
                                                   lws_extension_callback_pm_deflate,
                                                   "permessage-deflate"
                                                   "; client_no_context_takeover"
                                                   "; client_max_window_bits"},
                                                  {NULL, NULL, NULL /* terminator */}};

JSValue
minnet_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  int argind = 0, a = 0;
  BOOL is_tls = FALSE;
  MinnetServer* server;
  MinnetVhostOptions* mimetypes = 0;
  MinnetURL url = {0};
  JSValue ret, options;
  struct lws_context_creation_info* info;

  // SETLOG(LLL_INFO)

  if(!(server = js_mallocz(ctx, sizeof(MinnetServer))))
    return JS_ThrowOutOfMemory(ctx);

  info = &server->context.info;

  ret = JS_NewInt32(ctx, 0);
  options = argv[0];

  if(argc >= 2 && JS_IsString(argv[argind])) {
    const char* str;
    if((str = JS_ToCString(ctx, argv[argind]))) {
      url_parse(&url, str, ctx);

      // info->port = url.port;
      // info->vhost_name = js_strdup(ctx, url.host);
      //  info->listen_accept_protocol = js_strdup(ctx, url.protocol);
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
  } /* else {
     JSValue opt_private_key = JS_GetPropertyStr(ctx, options, "sslPrivateKey");

     if(JS_IsString(opt_private_key))
       is_tls = TRUE;

     JS_FreeValue(ctx, opt_private_key);
   }*/

  if(!JS_IsUndefined(opt_port)) {
    int32_t port;
    JS_ToInt32(ctx, &port, opt_port);
    info->port = port;
  } else {
    info->port = url.port;
  }

  if(JS_IsString(opt_host)) {
    info->vhost_name = js_to_string(ctx, opt_host);
  } else {
    info->vhost_name = js_strdup(ctx, url.host);
  }
  /* if(JS_IsString(opt_protocol)) {
     info->listen_accept_protocol = js_to_string(ctx, opt_protocol);
   } else {
     info->listen_accept_protocol = js_strdup(ctx, url.protocol);
   }*/

  GETCB(opt_on_pong, server->cb.pong)
  GETCB(opt_on_close, server->cb.close)
  GETCB(opt_on_connect, server->cb.connect)
  GETCB(opt_on_message, server->cb.message)
  GETCB(opt_on_fd, server->cb.fd)
  GETCB(opt_on_http, server->cb.http)

  for(int i = 0; i < countof(protocols); i++) protocols[i].user = ctx;

  server->context.js = ctx;
  server->context.error = JS_NULL;
  info->user = server;
  info->protocols = protocols2;
  // info->options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
  info->options = 0
      //| LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE
      ;
  info->user = server;

  info->options |= LWS_SERVER_OPTION_PEER_CERT_NOT_REQUIRED;
  info->options |= LWS_SERVER_OPTION_H2_JUST_FIX_WINDOW_UPDATE_OVERFLOW;
  info->options |= LWS_SERVER_OPTION_VH_H2_HALF_CLOSED_LONG_POLL;

  if(is_tls) {
    info->options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info->options |= /*LWS_SERVER_OPTION_REDIRECT_HTTP_TO_HTTPS | */ LWS_SERVER_OPTION_ALLOW_HTTP_ON_HTTPS_LISTENER | LWS_SERVER_OPTION_ALLOW_NON_SSL_ON_SSL_PORT;
  } /*

   if(!JS_IsUndefined(opt_port)) {
     int32_t port;
     JS_ToInt32(ctx, &port, opt_port);
     info->port = port;
   }

   if(JS_IsString(opt_host)) {
     if(info->vhost_name)
       js_free(ctx, (void*)info->vhost_name);
     info->vhost_name = js_to_string(ctx, opt_host);
   }
 */
  /*  if(!info->vhost_name)
      if((info->vhost_name = js_malloc(ctx, ((strlen(url.host) + 7) + 15) & ~0xf)))
        sprintf(info->vhost_name, "%s:%u", url.host, url.port);*/
  info->vhost_name = url_format((MinnetURL){.host = url.host, .port = url.port}, ctx);
  info->error_document_404 = "/404.html";
  info->mounts = &mount;

  if(is_tls)
    context_certificate(&server->context, options);

  if(JS_IsArray(ctx, opt_mimetypes)) {
    MinnetVhostOptions *vopts, **vop = (MinnetVhostOptions**)&mimetypes;
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

    for(pvo = mimetypes; pvo; pvo = pvo->next) {
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
        mount->extra_mimetypes = mimetypes;
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
        mount->extra_mimetypes = mimetypes;
        mount->pro = "http";
        ADD(m, mount, next);
        JS_FreeCString(ctx, name);
      }
    }
  }

  if(!(server->context.lws = lws_create_context(&server->context.info))) {
    lwsl_err("libwebsockets init failed\n");
    return JS_ThrowInternalError(ctx, "libwebsockets init failed");
  }

  /*    if(!lws_create_vhost(server->context.lws, &server->context.info)) {
      lwsl_err("Failed to create vhost\n");
      return JS_ThrowInternalError(ctx, "Failed to create vhost");
    }*/

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

  lws_context_destroy(server->context.lws);

  if(mimetypes) {
    MinnetVhostOptions *vhost_options, *next;

    for(vhost_options = mimetypes; vhost_options; vhost_options = next) {
      next = (MinnetVhostOptions*)vhost_options->lws.next;
      vhost_options_free(ctx, vhost_options);
    }
  }

  if(info->mounts) {
    const MinnetHttpMount *mount, *next;

    for(mount = (MinnetHttpMount*)info->mounts; mount; mount = next) {
      next = (MinnetHttpMount*)mount->lws.mount_next;
      mount_free(ctx, mount);
    }
  }

  js_buffer_free(&server->context.key, ctx);
  js_buffer_free(&server->context.crt, ctx);
  js_buffer_free(&server->context.ca, ctx);

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

int
defprot_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetSession* session = user;
  MinnetServer* server = session ? session->server : lws_context_user(lws_get_context(wsi));
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

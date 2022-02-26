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

#include "libwebsockets/plugins/raw-proxy/protocol_lws_raw_proxy.c"

THREAD_LOCAL MinnetServer minnet_server = {0};

int proxy_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int raw_client_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int ws_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int defprot_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int http_server_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

static struct lws_protocols protocols[] = {
    {"ws", ws_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    {"defprot", lws_callback_http_dummy, 0, 0},
    {"http", http_server_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    // {"proxy-ws", proxy_callback, 0, 1024, 0, NULL, 0},
    {"proxy-raw", raw_client_callback, 0, 1024, 0, NULL, 0},
    {0},
};

static struct lws_protocols protocols2[] = {
    {"ws", ws_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    {"defprot", defprot_callback, 0, 0},
    {"http", http_server_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    {"proxy-ws", proxy_callback, 0, 1024, 0, NULL, 0},
    {"proxy-raw", raw_client_callback, 0, 1024, 0, NULL, 0},
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

JSValue
minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  int argind = 0, a = 0, port = 7981;
  BOOL is_tls = FALSE;
  MinnetVhostOptions* mimetypes = 0;
  MinnetURL url = {0};
  JSValue ret, options;

  memset(&minnet_server, 0, sizeof minnet_server);

  lwsl_user("Minnet WebSocket Server\n");
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
  } else {
    JSValue opt_private_key = JS_GetPropertyStr(ctx, options, "sslPrivateKey");

    if(JS_IsString(opt_private_key))
      is_tls = TRUE;

    JS_FreeValue(ctx, opt_private_key);
  }

  if(!JS_IsUndefined(opt_port))
    JS_ToInt32(ctx, &port, opt_port);

  GETCB(opt_on_pong, minnet_server.cb.pong)
  GETCB(opt_on_close, minnet_server.cb.close)
  GETCB(opt_on_connect, minnet_server.cb.connect)
  GETCB(opt_on_message, minnet_server.cb.message)
  GETCB(opt_on_fd, minnet_server.cb.fd)
  GETCB(opt_on_http, minnet_server.cb.http)

  protocols[0].user = ctx;
  protocols[1].user = ctx;

  minnet_server.context.js = ctx;
  minnet_server.context.error = JS_NULL;
  minnet_server.context.info.user = &minnet_server;
  minnet_server.context.info.protocols = protocols2;
  // minnet_server.context.info.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
  minnet_server.context.info.options = 0
      //| LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE
      ;

  //  minnet_server.context.info.options |= LWS_SERVER_OPTION_H2_JUST_FIX_WINDOW_UPDATE_OVERFLOW;
  if(is_tls) {
    minnet_server.context.info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    minnet_server.context.info.options |= /*LWS_SERVER_OPTION_REDIRECT_HTTP_TO_HTTPS | */ LWS_SERVER_OPTION_ALLOW_HTTP_ON_HTTPS_LISTENER | LWS_SERVER_OPTION_ALLOW_NON_SSL_ON_SSL_PORT;
  }
  if(JS_IsString(opt_host))
    minnet_server.context.info.vhost_name = js_to_string(ctx, opt_host);
  else
    minnet_server.context.info.vhost_name = js_strdup(ctx, "localhost");

  minnet_server.context.info.port = port;
  minnet_server.context.info.error_document_404 = 0; // "/404.html";
  minnet_server.context.info.mounts = &mount;

  if(is_tls) {
    minnet_ws_sslcert(ctx, &minnet_server.context.info, options);
  }

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

  minnet_server.context.info.mounts = 0;
  {
    MinnetHttpMount** m = (MinnetHttpMount**)&minnet_server.context.info.mounts;

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

  if(!(minnet_server.context.lws = lws_create_context(&minnet_server.context.info))) {
    lwsl_err("libwebsockets init failed\n");
    return JS_ThrowInternalError(ctx, "libwebsockets init failed");
  }
  /*
    if(!lws_create_vhost(minnet_server.context.lws, &minnet_server.context.info)) {
      lwsl_err("Failed to create vhost\n");
      return JS_ThrowInternalError(ctx, "Failed to create vhost");
    }*/

  lws_service_adjust_timeout(minnet_server.context.lws, 1, 0);

  while(a >= 0) {
    if(!JS_IsNull(minnet_server.context.error)) {
      ret = JS_Throw(ctx, minnet_server.context.error);
      break;
    }

    if(minnet_server.cb.fd.ctx)
      js_std_loop(ctx);
    else
      a = lws_service(minnet_server.context.lws, 20);
  }

  lws_context_destroy(minnet_server.context.lws);

  if(mimetypes) {
    MinnetVhostOptions *vhost_options, *next;

    for(vhost_options = mimetypes; vhost_options; vhost_options = next) {
      next = (MinnetVhostOptions*)vhost_options->lws.next;
      vhost_options_free(ctx, vhost_options);
    }
  }

  if(minnet_server.context.info.mounts) {
    const MinnetHttpMount *mount, *next;

    for(mount = (MinnetHttpMount*)minnet_server.context.info.mounts; mount; mount = next) {
      next = (MinnetHttpMount*)mount->lws.mount_next;
      mount_free(ctx, mount);
    }
  }

  if(minnet_server.context.info.ssl_cert_filepath)
    JS_FreeCString(ctx, minnet_server.context.info.ssl_cert_filepath);

  if(minnet_server.context.info.ssl_private_key_filepath)
    JS_FreeCString(ctx, minnet_server.context.info.ssl_private_key_filepath);

  js_free(ctx, (void*)minnet_server.context.info.vhost_name);

  FREECB(minnet_server.cb.pong)
  FREECB(minnet_server.cb.close)
  FREECB(minnet_server.cb.connect)
  FREECB(minnet_server.cb.message)
  FREECB(minnet_server.cb.fd)
  FREECB(minnet_server.cb.http)

  return ret;
}

int
defprot_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {

  // if(!lws_is_poll_callback(reason)) printf("defprot_callback %s %p %p %zu\n", lws_callback_name(reason), user, in, len);

  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: {
      return 0;
    }
    case LWS_CALLBACK_ADD_POLL_FD: {
      struct lws_pollargs* args = in;
      if(minnet_server.cb.fd.ctx) {
        JSValue argv[3] = {JS_NewInt32(minnet_server.cb.fd.ctx, args->fd)};
        minnet_handlers(minnet_server.cb.fd.ctx, wsi, *args, &argv[1]);
        server_exception(&minnet_server, minnet_emit(&minnet_server.cb.fd, 3, argv));
        JS_FreeValue(minnet_server.cb.fd.ctx, argv[0]);
        JS_FreeValue(minnet_server.cb.fd.ctx, argv[1]);
        JS_FreeValue(minnet_server.cb.fd.ctx, argv[2]);
      }
      return 0;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {
      struct lws_pollargs* args = in;
      if(minnet_server.cb.fd.ctx) {
        JSValue argv[3] = {
            JS_NewInt32(minnet_server.cb.fd.ctx, args->fd),
        };
        minnet_handlers(minnet_server.cb.fd.ctx, wsi, *args, &argv[1]);
        server_exception(&minnet_server, minnet_emit(&minnet_server.cb.fd, 3, argv));
        JS_FreeValue(minnet_server.cb.fd.ctx, argv[0]);
        JS_FreeValue(minnet_server.cb.fd.ctx, argv[1]);
        JS_FreeValue(minnet_server.cb.fd.ctx, argv[2]);
      }
      return 0;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      struct lws_pollargs* args = in;
      if(minnet_server.cb.fd.ctx) {
        if(args->events != args->prev_events) {
          JSValue argv[3] = {JS_NewInt32(minnet_server.cb.fd.ctx, args->fd)};
          minnet_handlers(minnet_server.cb.fd.ctx, wsi, *args, &argv[1]);
          server_exception(&minnet_server, minnet_emit(&minnet_server.cb.fd, 3, argv));
          JS_FreeValue(minnet_server.cb.fd.ctx, argv[0]);
          JS_FreeValue(minnet_server.cb.fd.ctx, argv[1]);
          JS_FreeValue(minnet_server.cb.fd.ctx, argv[2]);
        }
      }
      return 0;
    }
  }

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

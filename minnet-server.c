#include "jsutils.h"
#include "minnet-websocket.h"
#include "minnet-server.h"
#include "minnet-server-http.h"
#include "minnet-response.h"
#include "minnet-request.h"
#include <list.h>
#include <quickjs-libc.h>
#include <libwebsockets.h>

MinnetServer minnet_server = {0};

int proxy_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int raw_client_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
int ws_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

static struct lws_protocols protocols[] = {
    {"ws", ws_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    {"http", http_callback, sizeof(MinnetSession), 1024, 0, NULL, 0},
    // {"proxy-ws", proxy_callback, 0, 1024, 0, NULL, 0},
    {"proxy-raw", raw_client_callback, 0, 1024, 0, NULL, 0},
    LWS_PROTOCOL_LIST_TERM,
};

JSValue
minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  int a = 0;
  int port = 7981;
  memset(&minnet_server, 0, sizeof minnet_server);

  lwsl_user("Minnet WebSocket Server\n");
  JSValue ret = JS_NewInt32(ctx, 0);
  JSValue options = argv[0];

  JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
  JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
  JSValue opt_on_pong = JS_GetPropertyStr(ctx, options, "onPong");
  JSValue opt_on_close = JS_GetPropertyStr(ctx, options, "onClose");
  JSValue opt_on_connect = JS_GetPropertyStr(ctx, options, "onConnect");
  JSValue opt_on_message = JS_GetPropertyStr(ctx, options, "onMessage");
  JSValue opt_on_fd = JS_GetPropertyStr(ctx, options, "onFd");
  JSValue opt_on_http = JS_GetPropertyStr(ctx, options, "onHttp");
  JSValue opt_mounts = JS_GetPropertyStr(ctx, options, "mounts");

  if(!JS_IsUndefined(opt_port))
    JS_ToInt32(ctx, &port, opt_port);

  if(JS_IsString(opt_host))
    minnet_server.info.vhost_name = js_to_string(ctx, opt_host);
  else
    minnet_server.info.vhost_name = js_strdup(ctx, "localhost");

  GETCB(opt_on_pong, minnet_server.cb_pong)
  GETCB(opt_on_close, minnet_server.cb_close)
  GETCB(opt_on_connect, minnet_server.cb_connect)
  GETCB(opt_on_message, minnet_server.cb_message)
  GETCB(opt_on_fd, minnet_server.cb_fd)
  GETCB(opt_on_http, minnet_server.cb_http)

  protocols[0].user = ctx;
  protocols[1].user = ctx;

  minnet_server.ctx = ctx;
  minnet_server.info.port = port;
  minnet_server.info.protocols = protocols;
  minnet_server.info.mounts = 0;
  minnet_server.info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_SERVER_OPTION_ALLOW_HTTP_ON_HTTPS_LISTENER |
                               LWS_SERVER_OPTION_ALLOW_NON_SSL_ON_SSL_PORT /*| LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE*/;

  minnet_ws_sslcert(ctx, &minnet_server.info, options);

  if(JS_IsArray(ctx, opt_mounts)) {
    MinnetHttpMount** ptr = (MinnetHttpMount**)&minnet_server.info.mounts;
    uint32_t i;

    for(i = 0;; i++) {
      JSValue mount = JS_GetPropertyUint32(ctx, opt_mounts, i);

      if(JS_IsUndefined(mount))
        break;

      ADD(ptr, mount_new(ctx, mount), next);
    }
  }

  if(!(minnet_server.context = lws_create_context(&minnet_server.info))) {
    lwsl_err("libwebsockets init failed\n");
    return JS_EXCEPTION;
  }

  lws_service_adjust_timeout(minnet_server.context, 1, 0);

  while(a >= 0) {
    if(minnet_exception) {
      ret = JS_EXCEPTION;
      break;
    }

    if(minnet_server.cb_fd.ctx)
      js_std_loop(ctx);
    else
      a = lws_service(minnet_server.context, 20);
  }

  lws_context_destroy(minnet_server.context);

  if(minnet_server.info.mounts) {
    const MinnetHttpMount *mount, *next;

    for(mount = (MinnetHttpMount*)minnet_server.info.mounts; mount; mount = next) {
      next = (MinnetHttpMount*)mount->lws.mount_next;
      mount_free(ctx, mount);
    }
  }

  if(minnet_server.info.ssl_cert_filepath)
    JS_FreeCString(ctx, minnet_server.info.ssl_cert_filepath);

  if(minnet_server.info.ssl_private_key_filepath)
    JS_FreeCString(ctx, minnet_server.info.ssl_private_key_filepath);

  js_free(ctx, (void*)minnet_server.info.vhost_name);

  FREECB(minnet_server.cb_pong)
  FREECB(minnet_server.cb_close)
  FREECB(minnet_server.cb_connect)
  FREECB(minnet_server.cb_message)
  FREECB(minnet_server.cb_fd)
  FREECB(minnet_server.cb_http)

  return ret;
}

int
http_headers(JSContext* ctx, MinnetBuffer* headers, struct lws* wsi) {
  int tok, len, count = 0;

  if(!headers->start)
    buffer_alloc(headers, 1024, ctx);

  for(tok = WSI_TOKEN_HOST; tok < WSI_TOKEN_COUNT; tok++) {
    if(tok == WSI_TOKEN_HTTP)
      continue;

    if((len = lws_hdr_total_length(wsi, tok)) > 0) {
      char hdr[len + 1];
      const char* name = (const char*)lws_token_to_string(tok);
      int namelen = byte_chr(name, strlen(name), ':');
      lws_hdr_copy(wsi, hdr, len + 1, tok);
      hdr[len] = '\0';
      // printf("headers %i %.*s '%s'\n", tok, namelen, name, hdr);
      while(!buffer_printf(headers, "%.*s: %s\n", namelen, name, hdr)) buffer_grow(headers, 1024, ctx);
      ++count;
    }
  }
  return count;
}

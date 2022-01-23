#define _GNU_SOURCE
#include "minnet-client-http.h"
#include "minnet-client.h"
#include "minnet-websocket.h"
#include "minnet-request.h"
#include "minnet-response.h"
#include "minnet-url.h"
#include "minnet.h"
#include "jsutils.h"
#include <quickjs-libc.h>
#include <strings.h>

// static MinnetCallback client_cb_message, client_cb_connect, client_cb_close, client_cb_pong, client_cb_fd;

static int client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

THREAD_LOCAL JSContext* minnet_client_ctx = 0;

static const struct lws_protocols client_protocols[] = {
    {"http", http_client_callback, sizeof(MinnetSession), 0, 0, 0, 0},
    {"ws", client_callback, sizeof(MinnetSession), 0, 0, 0, 0},
    {"raw", client_callback, sizeof(MinnetSession), 0, 0, 0, 0},
    {0},
};

static JSValue
close_status(JSContext* ctx, const char* in, size_t len) {
  if(len >= 2)
    return JS_NewInt32(ctx, ((uint8_t*)in)[0] << 8 | ((uint8_t*)in)[1]);
  return JS_UNDEFINED;
}

static JSValue
close_reason(JSContext* ctx, const char* in, size_t len) {
  if(len > 2)
    return JS_NewStringLen(ctx, &in[2], len - 2);
  return JS_UNDEFINED;
}

static void
sslcert_client(JSContext* ctx, struct lws_context_creation_info* info, JSValueConst options) {
  JSValue opt_ssl_cert = JS_GetPropertyStr(ctx, options, "sslCert");
  JSValue opt_ssl_private_key = JS_GetPropertyStr(ctx, options, "sslPrivateKey");
  JSValue opt_ssl_ca = JS_GetPropertyStr(ctx, options, "sslCA");

  if(JS_IsString(opt_ssl_cert))
    info->ssl_cert_filepath = JS_ToCString(ctx, opt_ssl_cert);
  if(JS_IsString(opt_ssl_private_key))
    info->ssl_private_key_filepath = JS_ToCString(ctx, opt_ssl_private_key);
  if(JS_IsString(opt_ssl_ca))
    info->ssl_ca_filepath = JS_ToCString(ctx, opt_ssl_ca);
}

JSValue
minnet_ws_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct lws_context* context = 0;
  struct lws_context_creation_info info;
  int n = 0, argind = 0;
  JSValue ret = JS_NULL;
  MinnetClient client = {JS_UNDEFINED, JS_UNDEFINED};
  JSValue options = argv[0];
  struct lws* wsi = 0;
  const char *str, *url = 0, *method_str = 0;
  JSValue value;

  SETLOG(LLL_INFO)

  client.ctx = minnet_client_ctx = ctx;

  memset(&info, 0, sizeof info);
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info.options |= LWS_SERVER_OPTION_H2_JUST_FIX_WINDOW_UPDATE_OVERFLOW;
  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = client_protocols;
  info.user = &client;

  if(argc >= 2 && JS_IsString(argv[argind])) {
    url = JS_ToCString(ctx, argv[argind]);
    argind++;
  }

  options = argv[argind];

  if(!JS_IsObject(options))
    return JS_ThrowTypeError(ctx, "argument %d must be options object", argind + 1);

  url_from(&client.url, options, ctx);

  value = JS_GetPropertyStr(ctx, options, "method");
  str = JS_ToCString(ctx, value);
  method_str = js_strdup(ctx, JS_IsString(value) ? str : method_string(METHOD_GET));
  JS_FreeValue(ctx, value);
  JS_FreeCString(ctx, str);

  if(url) {
    url_parse(&client.url, url, ctx);
    JS_FreeCString(ctx, url);
  }

  GETCBPROP(options, "onPong", client.cb_pong)
  GETCBPROP(options, "onClose", client.cb_close)
  GETCBPROP(options, "onConnect", client.cb_connect)
  GETCBPROP(options, "onMessage", client.cb_message)
  GETCBPROP(options, "onFd", client.cb_fd)
  GETCBPROP(options, "onHttp", client.cb_http)

  if(!context) {
    sslcert_client(ctx, &info, options);

    if(!(context = lws_create_context(&info))) {
      lwsl_err("minnet-client: libwebsockets init failed\n");
      return JS_ThrowInternalError(ctx, "minnet-client: libwebsockets init failed");
    }
  }

  {
    char* url = url_format(&client.url, ctx);
    client.request = request_new(ctx, url_location(&client.url, ctx), url, method_number(method_str));
    client.headers = JS_GetPropertyStr(ctx, options, "headers");
    client.body = JS_GetPropertyStr(ctx, options, "body");
  }

  url_info(&client.url, &client.info);
  client.info.pwsi = &wsi;
  client.info.context = context;

  switch(protocol_number(client.url.protocol)) {
    case PROTOCOL_HTTP:
    case PROTOCOL_HTTPS: {
      client.info.method = method_str;
      break;
    }
  }

  printf("METHOD: %s\n", method_str);
  printf("PROTOCOL: %s\n", client.info.protocol);

  lws_client_connect_via_info(&client.info);

  minnet_exception = FALSE;

  while(n >= 0) {
    if(minnet_exception) {
      ret = JS_EXCEPTION;
      break;
    }

    js_std_loop(ctx);

    //  lws_service(context, 500);
  }
  if(wsi) {
    JSValue opt_binary = JS_GetPropertyStr(ctx, options, "binary");
    if(JS_IsBool(opt_binary)) {
      MinnetWebsocket* ws = minnet_ws_data2(ctx, ret);
      ws->binary = JS_ToBool(ctx, opt_binary);
    }
  } else {
    ret = JS_ThrowInternalError(ctx, "No websocket!");
  }
  url_free(ctx, &client.url);
  js_free(ctx, method_str);

  return ret;
}

uint8_t*
scan_backwards(uint8_t* ptr, uint8_t ch) {
  if(ptr[-1] == '\n') {
    do { --ptr; } while(ptr[-1] != ch);
    return ptr;
  }
  return 0;
}

static int
client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetHttpMethod method = -1;
  MinnetSession* cli = user;
  MinnetClient* client = lws_context_user(lws_get_context(wsi));
  JSContext* ctx = client->ctx;
  int n;

  lwsl_user("client " FG("%d") "%-25s" NC " is_ssl=%i len=%zu in='%.*s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, lws_is_ssl(wsi), len, (int)MIN(len, 32), (char*)in);

  switch(reason) {
    case LWS_CALLBACK_PROTOCOL_INIT: {
      return 0;
    }
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: {
      return 0;
    }
    case LWS_CALLBACK_WS_CLIENT_BIND_PROTOCOL:
    case LWS_CALLBACK_RAW_SKT_BIND_PROTOCOL: {
      break;
    }
    case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL:
    case LWS_CALLBACK_RAW_SKT_DROP_PROTOCOL: {
      break;
    }

    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
    case LWS_CALLBACK_CONNECTING: {
      return 0;
    }
    case LWS_CALLBACK_WSI_CREATE:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: {
      return 0;
    }
    case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_RAW_CLOSE:
    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      if(client->cb_close.ctx) {
        struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);
        int err = opaque ? opaque->error : 0;
        JSValueConst cb_argv[] = {
            minnet_ws_object(ctx, wsi),
            close_status(ctx, in, len),
            close_reason(ctx, in, len),
            JS_NewInt32(ctx, err),
        };
        minnet_emit(&client->cb_close, 4, cb_argv);
        JS_FreeValue(ctx, cb_argv[0]);
        JS_FreeValue(ctx, cb_argv[1]);
        JS_FreeValue(ctx, cb_argv[2]);
        JS_FreeValue(ctx, cb_argv[3]);
      }
      break;
    }
    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
    case LWS_CALLBACK_RAW_CONNECTED: {
      /* struct lws_context* lwsctx = lws_get_context(wsi);
       MinnetClient* client = lws_context_user(lwsctx);
 */
      if(cli && !cli->connected) {
        const char* method = client->info.method;

        if(!minnet_ws_data(cli->ws_obj))
          cli->ws_obj = minnet_ws_object(ctx, wsi);

        cli->connected = TRUE;
        if(!minnet_ws_data(cli->req_obj))
          cli->req_obj = minnet_request_wrap(ctx, client->request);

        // lwsl_user("client   " FGC(171, "%-25s") " fd=%i, in=%.*s\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);
        minnet_emit(&client->cb_connect, 2, &cli->ws_obj);

        cli->resp_obj = minnet_response_new(ctx, "/", /* method == METHOD_POST ? 201 :*/ 200, TRUE, "text/html");
        /**/
      }
      break;
    }

    case LWS_CALLBACK_CLIENT_WRITEABLE:
    case LWS_CALLBACK_RAW_WRITEABLE: {

      // lws_callback_on_writable(wsi);
      break;
    }

    case LWS_CALLBACK_RECEIVE:
    case LWS_CALLBACK_CLIENT_RECEIVE:
    case LWS_CALLBACK_RAW_RX: {
      if((client->cb_message.ctx = ctx)) {
        MinnetWebsocket* ws = minnet_ws_data2(ctx, cli->ws_obj);

        JSValue msg = ws->binary ? JS_NewArrayBufferCopy(ctx, in, len) : JS_NewStringLen(ctx, in, len);
        JSValue cb_argv[2] = {cli->ws_obj, msg};
        minnet_emit(&client->cb_message, 2, cb_argv);
      }
      return 0;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
      if((client->cb_pong.ctx = ctx)) {
        JSValue data = JS_NewArrayBufferCopy(client->cb_pong.ctx, in, len);
        JSValue cb_argv[2] = {cli->ws_obj, data};
        minnet_emit(&client->cb_pong, 2, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL:
    case LWS_CALLBACK_ADD_POLL_FD:
    case LWS_CALLBACK_DEL_POLL_FD:
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      return fd_callback(wsi, reason, &client->cb_fd, in);
    }

    default: {
      // minnet_lws_unhandled(reason);
      break;
    }
  }

  if(reason < LWS_CALLBACK_ADD_POLL_FD || reason > LWS_CALLBACK_UNLOCK_POLL)
    lwsl_notice("client  %-25s fd=%i, in='%.*s'\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);
  return 0;
}

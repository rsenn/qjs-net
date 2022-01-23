#define _GNU_SOURCE
#include "minnet-client.h"
#include "minnet-websocket.h"
#include "minnet-request.h"
#include "minnet-response.h"
#include "minnet-url.h"
#include "minnet.h"
#include <quickjs-libc.h>
#include <strings.h>

static MinnetCallback client_cb_message, client_cb_connect, client_cb_close, client_cb_pong, client_cb_fd;

static int client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

// THREAD_LOCAL struct lws_context* context = 0;
THREAD_LOCAL JSContext* minnet_client_ctx = 0;

static const struct lws_protocols client_protocols[] = {
    {"ws", client_callback, sizeof(MinnetSession), 0, 0, 0, 0},
    {"http", client_callback, sizeof(MinnetSession), 0, 0, 0, 0},
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
    info->client_ssl_cert_filepath = JS_ToCString(ctx, opt_ssl_cert);
  if(JS_IsString(opt_ssl_private_key))
    info->client_ssl_private_key_filepath = JS_ToCString(ctx, opt_ssl_private_key);
  if(JS_IsString(opt_ssl_ca))
    info->client_ssl_ca_filepath = JS_ToCString(ctx, opt_ssl_ca);
} 

JSValue
minnet_ws_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct lws_context* context = 0;
  struct lws_context_creation_info info;
  int n = 0;
  JSValue ret = JS_NULL;
  MinnetURL url;
  int raw = -1, ssl = -1;
  JSValue options = argv[0];
  struct lws* wsi = 0;

  SETLOG(LLL_INFO)

  minnet_client_ctx = ctx;

  memset(&info, 0, sizeof info);
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info.options |= LWS_SERVER_OPTION_H2_JUST_FIX_WINDOW_UPDATE_OVERFLOW;
  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = client_protocols;

  if(argc >= 2) {
    const char* urlStr = JS_ToCString(ctx, argv[0]);
    url = url_parse(ctx, urlStr);
    JS_FreeCString(ctx, urlStr);
    options = argv[1];
  } else {
    const char *host, *path = 0, *protocol = 0;
    int32_t port = -1;

    JSValue opt_protocol = JS_GetPropertyStr(ctx, options, "protocol");
    JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
    JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
    JSValue opt_ssl = JS_GetPropertyStr(ctx, options, "ssl");
    JSValue opt_raw = JS_GetPropertyStr(ctx, options, "raw");
    JSValue opt_path = JS_GetPropertyStr(ctx, options, "path");

    if(JS_IsString(opt_protocol))
      protocol = JS_ToCString(ctx, opt_protocol);

    host = JS_ToCString(ctx, opt_host);

    if(JS_IsString(opt_path))
      path = JS_ToCString(ctx, opt_path);

    if(JS_IsNumber(opt_port))
      JS_ToInt32(ctx, &port, opt_port);

    ssl = JS_ToBool(ctx, opt_ssl);
    raw = JS_ToBool(ctx, opt_raw);

    url = url_init(ctx, protocol, host, port, path);

    if(protocol)
      JS_FreeCString(ctx, protocol);
    JS_FreeCString(ctx, host);
    if(path)
      JS_FreeCString(ctx, path);

    JS_FreeValue(ctx, opt_protocol);
    JS_FreeValue(ctx, opt_path);
    JS_FreeValue(ctx, opt_ssl);
    JS_FreeValue(ctx, opt_raw);
    JS_FreeValue(ctx, opt_host);
    JS_FreeValue(ctx, opt_port);
  }

  GETCBPROP(options, "onPong", client_cb_pong)
  GETCBPROP(options, "onClose", client_cb_close)
  GETCBPROP(options, "onConnect", client_cb_connect)
  GETCBPROP(options, "onMessage", client_cb_message)
  GETCBPROP(options, "onFd", client_cb_fd)

  if(!context) {
    sslcert_client(ctx, &info, options);

    ;
    if(!(context = lws_create_context(&info))) {
      lwsl_err("minnet-client: libwebsockets init failed\n");
      return JS_ThrowInternalError(ctx, "minnet-client: libwebsockets init failed");
    }
  }
  if(url.protocol) {
    if(ssl == -1)
      ssl = !strcmp(url.protocol, "wss") || !strcmp(url.protocol, "https");

    if(raw == -1)
      raw = !strcasecmp(url.protocol, "raw");
  }

  url_connect( &url,context,  &wsi);

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
  url_free(ctx, &url);
  return ret;
}

static int
client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  JSContext* ctx = minnet_client_ctx;
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE];
  MinnetHttpMethod method = -1;
  MinnetSession* cli = user;

  lwsl_user("client " FG("%d") "%-25s" NC " is_ssl=%i len=%zu in='%.*s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, lws_is_ssl(wsi), len, (int)MIN(len, 32), (char*)in);

  switch(reason) {

    case LWS_CALLBACK_PROTOCOL_INIT: {

      return 0;
    }

    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: {

      return 0;
    }
    case LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_WS_CLIENT_BIND_PROTOCOL:
    case LWS_CALLBACK_RAW_SKT_BIND_PROTOCOL: {
      return 0;
    }
    case LWS_CALLBACK_CLIENT_HTTP_DROP_PROTOCOL:
    case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL:
    case LWS_CALLBACK_RAW_SKT_DROP_PROTOCOL: {
      return 0;
    }

    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
    case LWS_CALLBACK_CONNECTING: {
      return 0;
    }
    case LWS_CALLBACK_WSI_CREATE:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: {
      return 0;
    }
    case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
    case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_RAW_CLOSE:
    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      if((client_cb_close.ctx = ctx)) {
        struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);
        int err = opaque ? opaque->error : 0;
        JSValueConst cb_argv[] = {
            minnet_ws_object(ctx, wsi),
            close_status(ctx, in, len),
            close_reason(ctx, in, len),
            JS_NewInt32(ctx, err),
        };
        minnet_emit(&client_cb_close, 4, cb_argv);
        JS_FreeValue(ctx, cb_argv[0]);
        JS_FreeValue(ctx, cb_argv[1]);
        JS_FreeValue(ctx, cb_argv[2]);
        JS_FreeValue(ctx, cb_argv[3]);
      }
      break;
    }
    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
    case LWS_CALLBACK_RAW_CONNECTED: {
      if(!cli->connected) {
        cli->ws_obj = minnet_ws_object(ctx, wsi);

        // lwsl_user("client   " FGC(171, "%-25s") " fd=%i, in=%.*s\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);

        if((client_cb_connect.ctx = ctx)) {
          JSValue ws_obj = minnet_ws_object(ctx, wsi);
          minnet_emit(&client_cb_connect, 1, &ws_obj);
        }
        cli->connected = TRUE;
        cli->req_obj = JS_UNDEFINED;
        cli->resp_obj = minnet_response_new(ctx, "/", /* method == METHOD_POST ? 201 :*/ 200, TRUE, "text/html");
      }
      break;
    }
    case LWS_CALLBACK_HTTP_WRITEABLE:
    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
    case LWS_CALLBACK_CLIENT_WRITEABLE:
    case LWS_CALLBACK_RAW_WRITEABLE: {
      // lws_callback_on_writable(wsi);
      break;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: {
      char buffer[1024 + LWS_PRE];
      char* buf = buffer + LWS_PRE;
      int ret, len = sizeof(buffer) - LWS_PRE;
      // lwsl_user("http#1  " FGC(171, "%-25s") " fd=%d buf=%p len=%d\n", lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), buf, len);
      ret = lws_http_client_read(wsi, &buf, &len);
      // lwsl_user("http#2  " FGC(171, "%-25s") " fd=%d ret=%d buf=%p len=%d\n", lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), ret, buf, len);
      if(ret)
        return -1;

      if(!cli->responded) {
        cli->responded = TRUE;
        // if(JS_IsUndefined(cli->resp_obj)) cli->resp_obj = minnet_response_new(ctx, "/", /* method == METHOD_POST ? 201 :*/ 200, TRUE, "text/html");
      }
      return 0;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: {
      // lwsl_user("http#read  " FGC(171, "%-25s") " " FGC(226, "fd=%d") " " FGC(87, "len=%zu") " " FGC(125, "in='%.*s'") "\n", lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), len,
      // (int)MIN(len, 32), (char*)in);
      MinnetResponse* resp = minnet_response_data2(ctx, cli->resp_obj);
      buffer_append(&resp->body, in, len, ctx);
      return 0;
    }

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: {
      MinnetResponse* resp = minnet_response_data2(ctx, cli->resp_obj);
      cli->done = TRUE;
      in = resp->body.read;
      len = buffer_REMAIN(&resp->body);
    }
    case LWS_CALLBACK_RECEIVE:
    case LWS_CALLBACK_CLIENT_RECEIVE:
    case LWS_CALLBACK_RAW_RX: {
      if((client_cb_message.ctx = ctx)) {
        MinnetWebsocket* ws = minnet_ws_data2(ctx, cli->ws_obj);
        JSValue ws_obj = minnet_ws_object(ctx, wsi);
        JSValue msg = ws->binary ? JS_NewArrayBufferCopy(ctx, in, len) : JS_NewStringLen(ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        minnet_emit(&client_cb_message, 2, cb_argv);
      }
      return 0;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
      if((client_cb_pong.ctx = ctx)) {
        JSValue ws_obj = minnet_ws_object(client_cb_pong.ctx, wsi);
        JSValue data = JS_NewArrayBufferCopy(client_cb_pong.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, data};
        minnet_emit(&client_cb_pong, 2, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL:
    case LWS_CALLBACK_ADD_POLL_FD:
    case LWS_CALLBACK_DEL_POLL_FD:
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      return fd_callback(wsi, reason, &client_cb_fd, in);
    }

    default: {
      // minnet_lws_unhandled(reason);
      break;
    }
  }

  if(reason < LWS_CALLBACK_ADD_POLL_FD || reason > LWS_CALLBACK_UNLOCK_POLL)
    lwsl_notice("client  %-25s fd=%i, in='%.*s'\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);

  return 0;
  //  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

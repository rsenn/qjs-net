#include "minnet-client.h"
#include "minnet-websocket.h"
#include <quickjs-libc.h>

static MinnetCallback client_cb_message, client_cb_connect, client_cb_close, client_cb_pong, client_cb_fd;

static int client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

THREAD_LOCAL struct lws_context* minnet_client_lws = 0;
THREAD_LOCAL JSContext* minnet_client_ctx = 0;

static const struct lws_protocols client_protocols[] = {
    {"ws", client_callback, sizeof(MinnetClient), 0, 0, 0, 0},
    {"http", client_callback, sizeof(MinnetClient), 0, 0, 0, 0},
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

static int
connect_client(struct lws_context* context, MinnetURL* url, BOOL ssl, BOOL raw, struct lws** p_wsi) {
  struct lws_client_connect_info i;

  memset(&i, 0, sizeof(i));

  i.protocol = "ws";
  if(raw) {
    i.method = "RAW";
  } else if(!strncmp(url->protocol, "http", 4)) {
    i.alpn = "http/1.1";
    i.method = "GET";
    i.protocol = "http";
  }

  i.context = context;
  i.port = url->port;
  i.address = url->host;
  if(ssl) {
    i.ssl_connection = LCCSCF_USE_SSL | LCCSCF_H2_QUIRK_OVERFLOWS_TXCR | LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM;
    i.ssl_connection |= LCCSCF_ALLOW_SELFSIGNED;
    i.ssl_connection |= LCCSCF_ALLOW_INSECURE;
  }
  i.path = url->location;
  i.host = i.address;
  i.origin = i.address;
  i.pwsi = p_wsi;

  url->host = 0;
  url->location = 0;

  return !lws_client_connect_via_info(&i);
}

JSValue
minnet_ws_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  struct lws_context_creation_info info;
  int n = 0;
  JSValue ret = JS_NULL;
  MinnetURL url;
  BOOL raw = FALSE;
  JSValue options = argv[0];
  struct lws* wsi = 0;

  SETLOG(LLL_INFO)

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
    const char *host, *path = 0;
    int32_t port = -1, ssl = -1;

    JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
    JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
    JSValue opt_ssl = JS_GetPropertyStr(ctx, options, "ssl");
    JSValue opt_raw = JS_GetPropertyStr(ctx, options, "raw");
    JSValue opt_path = JS_GetPropertyStr(ctx, options, "path");

    host = JS_ToCString(ctx, opt_host);

    if(JS_IsString(opt_path))
      path = JS_ToCString(ctx, opt_path);

    if(JS_IsNumber(opt_port))
      JS_ToInt32(ctx, &port, opt_port);

    ssl = JS_ToBool(ctx, opt_ssl);
    raw = JS_ToBool(ctx, opt_raw);

    url = url_init(ctx, ssl ? "wss" : "ws", host, port, path);

    JS_FreeCString(ctx, host);
    if(path)
      JS_FreeCString(ctx, path);

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

  if(!minnet_client_lws) {
    sslcert_client(ctx, &info, options);

    minnet_client_lws = lws_create_context(&info);
    if(!minnet_client_lws) {
      lwsl_err("Libwebsockets init failed\n");
      return JS_ThrowInternalError(ctx, "Libwebsockets init failed");
    }
  }

  minnet_client_ctx = ctx;

  connect_client(minnet_client_lws, &url, !strcmp(url.protocol, "wss") || !strcmp(url.protocol, "https"), raw, &wsi);

  minnet_exception = FALSE;
  while(n >= 0) {
    if(minnet_exception) {
      ret = JS_EXCEPTION;
      break;
    }
    if(wsi)
      break;
    js_std_loop(ctx);

    //  lws_service(minnet_client_lws, 500);
  }
  if(wsi) {
    ret = minnet_ws_object(ctx, wsi);
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
  MinnetClient* cli = user;

  lwsl_user("client " FG("%d") "%-25s" NC " is_ssl=%i in='%.*s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, lws_is_ssl(wsi), (int)MIN(len, 32), (char*)in);

  switch(reason) {
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
      buffer_free(&cli->body, JS_GetRuntime(minnet_client_ctx));
      JS_FreeValue(minnet_client_ctx, cli->ws_obj);
      return 0;
    }

    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
    case LWS_CALLBACK_CONNECTING:
    case LWS_CALLBACK_PROTOCOL_INIT: {
      return 0;
    }
    case LWS_CALLBACK_WSI_CREATE:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: {
      buffer_zero(&cli->body);
      cli->ws_obj = minnet_ws_object(minnet_client_ctx, wsi);

      return 0;
    }
    case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
    case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_RAW_CLOSE:
    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      JSContext* ctx = client_cb_close.ctx;
      if(ctx) {
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
        lwsl_user("client   " FGC(171, "%-25s") " fd=%i, in=%.*s\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);

        if(client_cb_connect.ctx) {
          JSValue ws_obj = minnet_ws_object(client_cb_connect.ctx, wsi);
          minnet_emit(&client_cb_connect, 1, &ws_obj);
        }
        cli->connected = TRUE;
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
      int len = sizeof(buffer) - LWS_PRE;
      if(lws_http_client_read(wsi, &buf, &len))
        return -1;
      return 0;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: {
      buffer_append(&cli->body, in, len, minnet_client_ctx);
      return 0;
    }

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: {

      in = cli->body.read;
      len = buffer_REMAIN(&cli->body);
    }
    case LWS_CALLBACK_RECEIVE:
    case LWS_CALLBACK_CLIENT_RECEIVE:
    case LWS_CALLBACK_RAW_RX: {
      MinnetWebsocket* ws = minnet_ws_data(cli->ws_obj);

      if(client_cb_message.ctx) {
        JSValue ws_obj = minnet_ws_object(client_cb_message.ctx, wsi);
        JSValue msg = ws->binary ? JS_NewArrayBufferCopy(client_cb_message.ctx, in, len) : JS_NewStringLen(client_cb_message.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        minnet_emit(&client_cb_message, 2, cb_argv);
      }
      return 0;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
      if(client_cb_pong.ctx) {
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
    lwsl_user("client  %-25s fd=%i, in='%.*s'\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);

  return 0;
  //  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

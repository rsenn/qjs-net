#include "minnet-client.h"
#include "minnet-websocket.h"
#include <quickjs-libc.h>

static MinnetCallback client_cb_message, client_cb_connect, client_cb_close, client_cb_pong, client_cb_fd;

static int lws_client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

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

static int
connect_client(struct lws_context* context, const char* host, uint16_t port, BOOL ssl, const char* path) {
  struct lws_client_connect_info i;

  memset(&i, 0, sizeof(i));

  i.context = context;
  i.port = port;
  i.address = host;
  i.ssl_connection = ssl;
  i.path = path;
  i.host = i.address;
  i.origin = i.address;
  i.protocol = "ws";
  // i.pwsi = &client_wsi;

  return !lws_client_connect_via_info(&i);
}

static const struct lws_protocols client_protocols[] = {
    {"ws", lws_client_callback, 0, 0, 0, 0, 0},
    {0},
};

JSValue
minnet_ws_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  static struct lws_context* context;
  struct lws_context_creation_info info;
  int n = 0;
  JSValue ret = JS_NewInt32(ctx, 0);
  MinnetURL url;
  JSValue options = argv[0];

  SETLOG(LLL_INFO)

  memset(&info, 0, sizeof info);
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
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
    JSValue opt_path = JS_GetPropertyStr(ctx, options, "path");

    host = JS_ToCString(ctx, opt_host);

    if(JS_IsString(opt_path))
      path = JS_ToCString(ctx, opt_path);

    if(JS_IsNumber(opt_port))
      JS_ToInt32(ctx, &port, opt_port);

    ssl = JS_ToBool(ctx, opt_ssl);

    url = url_init(ctx, ssl ? "wss" : "ws", host, port, path);

    JS_FreeCString(ctx, host);
    if(path)
      JS_FreeCString(ctx, path);

    JS_FreeValue(ctx, opt_path);
    JS_FreeValue(ctx, opt_ssl);
    JS_FreeValue(ctx, opt_host);
    JS_FreeValue(ctx, opt_port);
  }

  JSValue opt_on_pong = JS_GetPropertyStr(ctx, options, "onPong");
  JSValue opt_on_close = JS_GetPropertyStr(ctx, options, "onClose");
  JSValue opt_on_connect = JS_GetPropertyStr(ctx, options, "onConnect");
  JSValue opt_on_message = JS_GetPropertyStr(ctx, options, "onMessage");
  JSValue opt_on_fd = JS_GetPropertyStr(ctx, options, "onFd");

  GETCB(opt_on_pong, client_cb_pong)
  GETCB(opt_on_close, client_cb_close)
  GETCB(opt_on_connect, client_cb_connect)
  GETCB(opt_on_message, client_cb_message)
  GETCB(opt_on_fd, client_cb_fd)

  minnet_ws_sslcert(ctx, &info, options);

  context = lws_create_context(&info);
  if(!context) {
    lwsl_err("Libwebsockets init failed\n");
    return JS_EXCEPTION;
  }

  connect_client(context, url.host, url.port, !strcmp(url.protocol, "wss"), url.location);

  minnet_exception = FALSE;

  while(n >= 0) {
    if(minnet_exception) {
      ret = JS_EXCEPTION;
      break;
    }

    if(client_cb_fd.ctx)
      js_std_loop(ctx);
    else
      /* n =*/lws_service(context, 500);
  }

  lws_context_destroy(context);

  JS_FreeValue(ctx, opt_on_pong);
  JS_FreeValue(ctx, opt_on_close);
  JS_FreeValue(ctx, opt_on_connect);
  JS_FreeValue(ctx, opt_on_message);
  JS_FreeValue(ctx, opt_on_fd);

  url_free(ctx, &url);

  return ret;
}

static int
lws_client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {

  switch(reason) {
    case LWS_CALLBACK_WSI_CREATE:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:  
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_CONNECTING:
    case LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_PROTOCOL_INIT: {
     return 0;
    }
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: {
     return 0;
    }
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
      break;
    }
    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {

      break;
    }
    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
    case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      JSContext* ctx = client_cb_close.ctx;
      if(ctx) {
        JSValueConst cb_argv[] = {
            minnet_ws_object(ctx, wsi),
            close_status(ctx, in, len),
            close_reason(ctx, in, len),
        };
        minnet_emit(&client_cb_close, 3, cb_argv);
        JS_FreeValue(ctx, cb_argv[0]);
        JS_FreeValue(ctx, cb_argv[1]);
        JS_FreeValue(ctx, cb_argv[2]);
      }
      break;
    }
    // case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_CLIENT_ESTABLISHED: {
      if(client_cb_connect.ctx) {
        JSValue ws_obj = minnet_ws_object(client_cb_connect.ctx, wsi);
        minnet_emit(&client_cb_connect, 1, &ws_obj);
      }
      break;
    }
    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      lws_callback_on_writable(wsi);
      break;
    }
      /*   case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL: {
           client_wsi = NULL;
           if(client_cb_close.ctx) {
             JSValue cb_argv[] = {minnet_ws_object(client_cb_close.ctx, wsi), JS_NewString(client_cb_close.ctx, in)};
             minnet_emit(&client_cb_close, 2, cb_argv);
             JS_FreeValue(client_cb_close.ctx, cb_argv[0]);
             JS_FreeValue(client_cb_close.ctx, cb_argv[1]);
           }
           break;
         }*/
    case LWS_CALLBACK_CLIENT_RECEIVE: {
      if(client_cb_message.ctx) {
        JSValue ws_obj = minnet_ws_object(client_cb_message.ctx, wsi);
        JSValue msg = JS_NewStringLen(client_cb_message.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        minnet_emit(&client_cb_message, 2, cb_argv);
      }
      break;
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
    case LWS_CALLBACK_ADD_POLL_FD: {
      struct lws_pollargs* args = in;
      if(client_cb_fd.ctx) {
        JSValue argv[3] = {JS_NewInt32(client_cb_fd.ctx, args->fd)};
        minnet_handlers(client_cb_fd.ctx, wsi, args, &argv[1]);

        minnet_emit(&client_cb_fd, 3, argv);
        JS_FreeValue(client_cb_fd.ctx, argv[0]);
        JS_FreeValue(client_cb_fd.ctx, argv[1]);
        JS_FreeValue(client_cb_fd.ctx, argv[2]);
      }
      break;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {
      struct lws_pollargs* args = in;
      if(client_cb_fd.ctx) {
        JSValue argv[3] = {
            JS_NewInt32(client_cb_fd.ctx, args->fd),
        };
        minnet_handlers(client_cb_fd.ctx, wsi, args, &argv[1]);
        minnet_emit(&client_cb_fd, 3, argv);
        JS_FreeValue(client_cb_fd.ctx, argv[0]);
      }
      break;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      struct lws_pollargs* args = in;

      if(client_cb_fd.ctx && args->events != args->prev_events) {
        JSValue argv[3] = {JS_NewInt32(client_cb_fd.ctx, args->fd)};
        minnet_handlers(client_cb_fd.ctx, wsi, args, &argv[1]);

        minnet_emit(&client_cb_fd, 3, argv);
        JS_FreeValue(client_cb_fd.ctx, argv[0]);
        JS_FreeValue(client_cb_fd.ctx, argv[1]);
        JS_FreeValue(client_cb_fd.ctx, argv[2]);
      }
      break;
    }

    default: {
      // minnet_lws_unhandled(reason);
      break;
    }
  }

  if(reason < LWS_CALLBACK_ADD_POLL_FD || reason > LWS_CALLBACK_UNLOCK_POLL)
    lwsl_user("ws   %-25s fd=%i, in='%.*s'\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);

return 0;
//  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

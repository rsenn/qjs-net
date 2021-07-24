#include "minnet-client.h"
#include "minnet-websocket.h"
#include "minnet.h"
#include "quickjs-libc.h"

static struct lws_context* client_context;
static struct lws* client_wsi;
static int client_server_port = 7981;
static const char* client_server_address = "localhost";

static MinnetWebsocketCallback client_cb_message, client_cb_connect,  client_cb_close, client_cb_pong, client_cb_fd;

static int lws_client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

static int
connect_client(void) {
  struct lws_client_connect_info i;

  memset(&i, 0, sizeof(i));

  i.context = client_context;
  i.port = client_server_port;
  i.address = client_server_address;
  i.path = "/";
  i.host = i.address;
  i.origin = i.address;
  i.ssl_connection = 0;
  i.protocol = "minnet";
  i.pwsi = &client_wsi;

  return !lws_client_connect_via_info(&i);
}

static const struct lws_protocols client_protocols[] = {
    {"minnet", lws_client_callback, 0, 0, 0, 0, 0},
    {NULL, NULL, 0, 0, 0, 0,0},
};

JSValue
minnet_ws_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  struct lws_context_creation_info info;
  int n = 0;
  JSValue ret = JS_NewInt32(ctx, 0);

  SETLOG

  memset(&info, 0, sizeof info);
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = client_protocols;

  JSValue options = argv[0];
  JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
  JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
  JSValue opt_on_pong = JS_GetPropertyStr(ctx, options, "onPong");
  JSValue opt_on_close = JS_GetPropertyStr(ctx, options, "onClose");
  JSValue opt_on_connect = JS_GetPropertyStr(ctx, options, "onConnect");
  JSValue opt_on_message = JS_GetPropertyStr(ctx, options, "onMessage");
  JSValue opt_on_fd = JS_GetPropertyStr(ctx, options, "onFd");

  if(JS_IsString(opt_host))
    client_server_address = JS_ToCString(ctx, opt_host);

  if(JS_IsNumber(opt_port))
    JS_ToInt32(ctx, &client_server_port, opt_port);

  GETCB(opt_on_pong, client_cb_pong)
  GETCB(opt_on_close, client_cb_close)
  GETCB(opt_on_connect, client_cb_connect)
  GETCB(opt_on_message, client_cb_message)
  GETCB(opt_on_fd, client_cb_fd)

  minnet_ws_sslcert(ctx, &info, options);

  client_context = lws_create_context(&info);
  if(!client_context) {
    lwsl_err("Libwebsockets init failed\n");
    return JS_EXCEPTION;
  }

  connect_client();
  minnet_exception = FALSE;

  while(n >= 0) {
    if(minnet_exception) {
      ret = JS_EXCEPTION;
      break;
    }

    if(client_cb_fd.func_obj)
      js_std_loop(ctx);
    else
      /* n =*/lws_service(client_context, 500);
  }

  lws_context_destroy(client_context);

  return ret;
}

static int
lws_client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  switch(reason) {
    case LWS_CALLBACK_PROTOCOL_INIT: {
      // connect_client();
      break;
    }
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: {
      break;
    }
    case LWS_CALLBACK_CONNECTING: {
      //   printf("LWS_CALLBACK_CONNECTING\n");
      break;
    }

    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
      break;
    }
    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {

      break;
    }
    case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      client_wsi = NULL;
      if(client_cb_close.func_obj) {
        JSValue why = in ? JS_NewString(client_cb_close.ctx, in) : JS_NULL;
        minnet_ws_emit(&client_cb_close, 1, &why);
      }
      break;
    }
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_CLIENT_ESTABLISHED: {
      if(client_cb_connect.func_obj) {
        JSValue ws_obj = minnet_ws_object(client_cb_connect.ctx, wsi);
        minnet_ws_emit(&client_cb_connect, 1, &ws_obj);
      }
      break;
    }
    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      lws_callback_on_writable(wsi);
      break;
    }
    case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL: {
      client_wsi = NULL;
      if(client_cb_close.func_obj) {
        JSValue cb_argv[] = {minnet_ws_object(client_cb_close.ctx, wsi), JS_NewString(client_cb_close.ctx, in)};
        minnet_ws_emit(&client_cb_close, 2, cb_argv);
        JS_FreeValue(client_cb_close.ctx, cb_argv[0]);
        JS_FreeValue(client_cb_close.ctx, cb_argv[1]);
      }
      break;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE: {
      if(client_cb_message.func_obj) {
        JSValue ws_obj = minnet_ws_object(client_cb_message.ctx, wsi);
        JSValue msg = JS_NewStringLen(client_cb_message.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        minnet_ws_emit(&client_cb_message, 2, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
      if(client_cb_pong.func_obj) {
        JSValue ws_obj = minnet_ws_object(client_cb_pong.ctx, wsi);
        JSValue data = JS_NewArrayBufferCopy(client_cb_pong.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, data};
        minnet_ws_emit(&client_cb_pong, 2, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_ADD_POLL_FD: {
      struct lws_pollargs* args = in;
      if(client_cb_fd.func_obj) {
        JSValue argv[3] = {JS_NewInt32(client_cb_fd.ctx, args->fd)};
        minnet_handlers(client_cb_fd.ctx, wsi, args, &argv[1]);

        minnet_ws_emit(&client_cb_fd, 3, argv);
        JS_FreeValue(client_cb_fd.ctx, argv[0]);
        JS_FreeValue(client_cb_fd.ctx, argv[1]);
        JS_FreeValue(client_cb_fd.ctx, argv[2]);
      }
      break;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {
      struct lws_pollargs* args = in;
      if(client_cb_fd.func_obj) {
        JSValue argv[3] = {
            JS_NewInt32(client_cb_fd.ctx, args->fd),
        };
        minnet_handlers(client_cb_fd.ctx, wsi, args, &argv[1]);
        minnet_ws_emit(&client_cb_fd, 3, argv);
        JS_FreeValue(client_cb_fd.ctx, argv[0]);
      }
      break;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      struct lws_pollargs* args = in;

      if(client_cb_fd.func_obj && args->events != args->prev_events) {
        JSValue argv[3] = {JS_NewInt32(client_cb_fd.ctx, args->fd)};
        minnet_handlers(client_cb_fd.ctx, wsi, args, &argv[1]);

        minnet_ws_emit(&client_cb_fd, 3, argv);
        JS_FreeValue(client_cb_fd.ctx, argv[0]);
        JS_FreeValue(client_cb_fd.ctx, argv[1]);
        JS_FreeValue(client_cb_fd.ctx, argv[2]);
      }
      break;
    }

    default: {
      // lws_print_unhandled(reason);
      break;
    }
  }

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

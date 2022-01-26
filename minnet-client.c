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

THREAD_LOCAL MinnetClient* minnet_client = 0;

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
  struct lws_context* lws = 0;
  int n = 0, argind = 0;
  JSValue ret = JS_NULL;
  MinnetClient client = {.headers = JS_UNDEFINED, .body = JS_UNDEFINED, .next = JS_UNDEFINED};
  struct lws_context_creation_info* info = &client.context.info;
  JSValue options = argv[0];
  struct lws* wsi = 0;
  const char *str, *url = 0, *method_str = 0;
  JSValue value;

  SETLOG(LLL_INFO)

  client.context.js = ctx;
  minnet_client = &client;

  memset(info, 0, sizeof(struct lws_context_creation_info));
  info->options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info->options |= LWS_SERVER_OPTION_H2_JUST_FIX_WINDOW_UPDATE_OVERFLOW;
  info->port = CONTEXT_PORT_NO_LISTEN;
  info->protocols = client_protocols;
  info->user = &client;

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

  GETCBPROP(options, "onPong", client.cb.pong)
  GETCBPROP(options, "onClose", client.cb.close)
  GETCBPROP(options, "onConnect", client.cb.connect)
  GETCBPROP(options, "onMessage", client.cb.message)
  GETCBPROP(options, "onFd", client.cb.fd)
  GETCBPROP(options, "onHttp", client.cb.http)

  if(!lws) {
    sslcert_client(ctx, info, options);

    if(!(lws = lws_create_context(info))) {
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

  {
    struct lws_client_connect_info* conn = &client.connect_info;
    MinnetWebsocket* ws;

    url_info(&client.url, conn);
    conn->pwsi = &wsi;
    conn->context = lws;

#ifdef DEBUG_OUTPUT
    printf("METHOD: %s\n", method_str);
    printf("PROTOCOL: %s\n", conn->protocol);
#endif

    switch(protocol_number(client.url.protocol)) {
      case PROTOCOL_HTTP:
      case PROTOCOL_HTTPS: {
        conn->method = method_str;
        break;
      }
    }

    lws_client_connect_via_info(conn);

    minnet_exception = FALSE;

    printf("WSI: %p\n", wsi);
    /*ws = ws_from_wsi(wsi);
    printf("WS: %p\n", ws);*/
    int status = -1;
    struct wsi_opaque_user_data* opaque = lws_opaque(wsi, client.context.js);

    //
    while(n >= 0) {
      if(status != opaque->status) {
        status = opaque->status;
        printf("STATUS: %s\n", ((const char*[]){"CONNECTING", "OPEN", "CLOSING", "CLOSED"})[status]);
      }

      if(status == CLOSED)
        break;

      if(minnet_exception) {
        minnet_exception = FALSE;
        ret = JS_EXCEPTION;
        break;
      }

      js_std_loop(ctx);
    }
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

  url_free(&client.url, ctx);
  js_free(ctx, method_str);

  minnet_client = NULL;

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
  MinnetSession* sess = user;
  MinnetClient* client = lws_context_user(lws_get_context(wsi));
  JSContext* ctx = client->context.js;
  struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);
  int n;

  if(lws_is_poll_callback(reason))
    return fd_callback(wsi, reason, &client->cb.fd, in);

  if(lws_is_http_callback(reason))
    return http_client_callback(wsi, reason, user, in, len);

  lwsl_user(len ? "client      " FG("%d") "%-38s" NC " is_ssl=%i len=%zu in='%.*s'\n" : "client      " FG("%d") "%-38s" NC " is_ssl=%i\n",
            22 + (reason * 2),
            lws_callback_name(reason) + 13,
            lws_is_ssl(wsi),
            len,
            (int)MIN(len, 32),
            (char*)in);

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
    case LWS_CALLBACK_CLIENT_CLOSED: {
      if(opaque->status < CLOSED) {
        opaque->status = CLOSED;
      }
    }
    case LWS_CALLBACK_RAW_CLOSE:
    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      if(opaque->status < CLOSING) {
        opaque->status = CLOSING;
        if((client->cb.close.ctx = ctx)) {
          int err = opaque ? opaque->error : 0;
          JSValueConst cb_argv[] = {sess->ws_obj, close_status(ctx, in, len), close_reason(ctx, in, len), JS_NewInt32(ctx, err)};
          minnet_emit(&client->cb.close, 4, cb_argv);
          // JS_FreeValue(ctx, cb_argv[0]);
          JS_FreeValue(ctx, cb_argv[1]);
          JS_FreeValue(ctx, cb_argv[2]);
          JS_FreeValue(ctx, cb_argv[3]);
        }
      }
      break;
    }
    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
    case LWS_CALLBACK_RAW_CONNECTED: {
      if(opaque->status < OPEN) {
        opaque->status = OPEN;

        sess->ws_obj = minnet_ws_wrap(ctx, wsi);
        sess->req_obj = minnet_request_wrap(ctx, client->request);
        sess->resp_obj = JS_NULL;

        // lwsl_user("client   " FGC(171, "%-38s") " fd=%i, in=%.*s\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);

        if((client->cb.connect.ctx = ctx))
          minnet_emit(&client->cb.connect, 2, &sess->ws_obj);

        /*if(!minnet_response_data(sess->resp_obj))
          sess->resp_obj = minnet_response_new(ctx, 0, 0, TRUE, 0);*/
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
      if((client->cb.message.ctx = ctx)) {
        MinnetWebsocket* ws = minnet_ws_data(sess->ws_obj);
        JSValue msg = ws->binary ? JS_NewArrayBufferCopy(ctx, in, len) : JS_NewStringLen(ctx, in, len);
        JSValue cb_argv[] = {sess->ws_obj, msg};
        minnet_emit(&client->cb.message, countof(cb_argv), cb_argv);
        JS_FreeValue(ctx, cb_argv[1]);
      }
      break;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
      if((client->cb.pong.ctx = ctx)) {
        JSValue data = JS_NewArrayBufferCopy(client->cb.pong.ctx, in, len);
        JSValue cb_argv[] = {sess->ws_obj, data};
        minnet_emit(&client->cb.pong, 2, cb_argv);
        JS_FreeValue(ctx, cb_argv[1]);
      }
      break;
    }

    default: {
      break;
    }
  }
  if(opaque && opaque->status >= CLOSING)
    return -1;

  return 0;
}

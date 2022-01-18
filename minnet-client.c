#include "minnet.h"
#include "minnet-client.h"
#include "minnet-websocket.h"
#include <quickjs-libc.h>

THREAD_LOCAL JSValue minnet_client_proto, minnet_client_ctor;
THREAD_LOCAL JSClassID minnet_client_class_id;

static int client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
static int http_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
static int raw_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

/*THREAD_LOCAL struct lws_context* minnet_client_lws = 0;
THREAD_LOCAL JSContext* minnet_client_ctx = 0;*/

static const struct lws_protocols client_protocols[] = {
    {"ws", client_callback, 0, 0, 0, 0, 0},
    {"http", client_callback, 0, 0, 0, 0, 0},
    {"raw", client_callback, 0, 0, 1024, 0, 0},
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

  if(raw || !strncmp(url->protocol, "raw", 3)) {
    i.method = "RAW";
    i.local_protocol_name = "raw";
  } else if(!strncmp(url->protocol, "http", 4)) {
    i.alpn = "http/1.1";
    i.method = "GET";
    i.protocol = "http";
  } else {
    i.protocol = "ws";
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
  lwsl_user("connect_client { protocol: %s, local_protocol_name: %s, host: %s, path: %s, origin: %s }\n", i.protocol, i.local_protocol_name, i.host, i.path, i.origin);

  return !lws_client_connect_via_info(&i);
}

JSValue
minnet_ws_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  int n = 0;
  JSValue ret = JS_NULL;
  BOOL raw = FALSE, block = TRUE;
  JSValue options = argv[0];
  MinnetClient* client;
  struct lws* wsi = 0;
  struct lws_context_creation_info* info;
  MinnetURL url;

  if(!(client = js_mallocz(ctx, sizeof(MinnetClient))))
    return JS_ThrowOutOfMemory(ctx);

  client->url = url;
  client->ctx = ctx;
  client->ws_obj = JS_NULL;
  client->req_obj = JS_NULL;
  client->resp_obj = JS_NULL;

  info = &client->info;

  SETLOG(LLL_INFO)

  info->options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info->options |= LWS_SERVER_OPTION_H2_JUST_FIX_WINDOW_UPDATE_OVERFLOW;
  info->port = CONTEXT_PORT_NO_LISTEN;
  info->protocols = client_protocols;
  info->user = client;

  if(argc >= 2 && JS_IsObject(argv[1])) {
    const char* urlStr = JS_ToCString(ctx, argv[0]);
    client->url = url_parse(ctx, urlStr);
    JS_FreeCString(ctx, urlStr);
    options = argv[1];
  }

  {
    const char *protocol = 0, *host = 0, *path = 0;
    int32_t port = -1, ssl = -1;

    JSValue opt_protocol = JS_GetPropertyStr(ctx, options, "protocol");
    JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
    JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
    JSValue opt_ssl = JS_GetPropertyStr(ctx, options, "ssl");
    JSValue opt_raw = JS_GetPropertyStr(ctx, options, "raw");
    JSValue opt_path = JS_GetPropertyStr(ctx, options, "path");
    JSValue opt_block = JS_GetPropertyStr(ctx, options, "block");

    protocol = JS_ToCString(ctx, opt_protocol);
    host = JS_ToCString(ctx, opt_host);

    if(JS_IsString(opt_path))
      path = JS_ToCString(ctx, opt_path);

    if(JS_IsNumber(opt_port))
      JS_ToInt32(ctx, &port, opt_port);

    ssl = JS_ToBool(ctx, opt_ssl);
    raw = JS_ToBool(ctx, opt_raw);
    if(!JS_IsUndefined(opt_block) && !JS_IsException(opt_block))
      block = JS_ToBool(ctx, opt_block);

    url = url_init(ctx, protocol ? protocol : ssl ? "wss" : "ws", host, port, path);

    JS_FreeCString(ctx, host);
    if(path)
      JS_FreeCString(ctx, path);

    JS_FreeValue(ctx, opt_path);
    JS_FreeValue(ctx, opt_ssl);
    JS_FreeValue(ctx, opt_raw);
    JS_FreeValue(ctx, opt_host);
    JS_FreeValue(ctx, opt_port);
  }

  sslcert_client(ctx, info, options);

  if(!(client->lws = lws_create_context(info))) {
    lwsl_err("client: Libwebsockets init failed\n");
    return JS_ThrowInternalError(ctx, "client: Libwebsockets init failed");
  }

  OPTIONS_CB(options, "onPong", client->cb_pong);
  OPTIONS_CB(options, "onClose", client->cb_close);
  OPTIONS_CB(options, "onConnect", client->cb_connect);
  OPTIONS_CB(options, "onMessage", client->cb_message);
  OPTIONS_CB(options, "onFd", client->cb_fd);

  connect_client(client->lws, &url, !strcmp(url.protocol, "wss") || !strcmp(url.protocol, "https"), raw, &wsi);

  client->ws_obj = minnet_ws_wrap(ctx, wsi);

  lws_set_opaque_user_data(wsi, client);

  minnet_exception = FALSE;

  if(!block) {
    ret = minnet_client_wrap(ctx, client);
    return;
  }

  if(block) {

    while(n >= 0) {
      if(minnet_exception) {
        ret = JS_EXCEPTION;
        break;
      }
      /* if(wsi)
         break;*/
      js_std_loop(ctx);

      //  lws_service(minnet_client_lws, 500);
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
  }

  // printf("minnet_ws_client onFd=%d\n", JS_VALUE_GET_TAG(client->cb_fd.func_obj));

  return ret;
}

static int
client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetClient* client;
  JSContext* ctx;

  if(reason >= LWS_CALLBACK_ADD_POLL_FD && reason <= LWS_CALLBACK_UNLOCK_POLL) {
    client = lws_context_user(lws_get_context(lws_get_network_wsi(wsi)));
    return fd_callback(wsi, reason, &client->cb_fd, in);
  }

  client = lws_get_opaque_user_data(wsi);
  ctx = client ? client->ctx : 0; //((MinnetClient*)lws_context_user(lws_get_context(wsi)))->ctx;

  lwsl_user("client_callback " FG("%d") "%-25s" NC " is_ssl=%i len=%d in='%.*s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, lws_is_ssl(wsi), (int)MIN(len, 32), (char*)in);

  /*  if(client && JS_VALUE_GET_TAG(client->ws_obj) == 0)
      client->ws_obj = minnet_ws_wrap(ctx, wsi);*/

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
      buffer_free(&client->body, JS_GetRuntime(ctx));
      JS_FreeValue(ctx, client->ws_obj);
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
      JSContext* ctx = client->cb_close.ctx;
      if(ctx) {
        struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);
        int err = opaque ? opaque->error : 0;
        JSValueConst cb_argv[] = {
            minnet_client_wrap(ctx, wsi),
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
    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
    case LWS_CALLBACK_RAW_CONNECTED: {
      if(!client->connected) {
        if(client->cb_connect.ctx) {
          JSValue ws_obj = minnet_client_wrap(client->cb_connect.ctx, wsi);
          minnet_emit(&client->cb_connect, 1, &ws_obj);
        }
        client->connected = TRUE;
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

      if((ret = lws_http_client_read(wsi, &buf, &len)))
        lwsl_user("RECEIVE_CLIENT_HTTP len=%d ret=%d\n", len, ret);
      if(ret)
        return -1;

      break;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: {
      lwsl_user("RECEIVE_CLIENT_HTTP_READ in=%.*s len=%i\n", len, in, len);
      buffer_append(&client->body, in, len, ctx);
      return 0;
    }

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: {

      in = client->body.read;
      len = buffer_REMAIN(&client->body);
    }
    case LWS_CALLBACK_RECEIVE:
    case LWS_CALLBACK_CLIENT_RECEIVE:
    case LWS_CALLBACK_RAW_RX: {
      MinnetWebsocket* ws = minnet_ws_data(client->ws_obj);

      if(client->cb_message.ctx) {
        JSValue msg = ws->binary ? JS_NewArrayBufferCopy(client->cb_message.ctx, in, len) : JS_NewStringLen(client->cb_message.ctx, in, len);
        JSValue cb_argv[2] = {client->ws_obj, msg};
        minnet_emit(&client->cb_message, 2, cb_argv);
      }
      return 0;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
      if(client->cb_pong.ctx) {
        JSValue ws_obj = minnet_client_wrap(client->cb_pong.ctx, wsi);
        JSValue data = JS_NewArrayBufferCopy(client->cb_pong.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, data};
        minnet_emit(&client->cb_pong, 2, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: {
      break;
    }
    case LWS_CALLBACK_ADD_POLL_FD:
    case LWS_CALLBACK_DEL_POLL_FD:
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      if(!client)
        client = lws_context_user(lws_get_context(lws_get_network_wsi(wsi)));
      MinnetCallback* cb_fd = &client->cb_fd;

      return fd_callback(wsi, reason, cb_fd, in);
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
JSValue
minnet_client_wrap(JSContext* ctx, MinnetClient* cli) {
  JSValue ret;

  ret = JS_NewObjectProtoClass(ctx, minnet_client_proto, minnet_client_class_id);

  JS_SetOpaque(ret, cli);
  return ret;
}

static JSValue
minnet_client_getter(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetClient* cli;
  JSValue ret = JS_UNDEFINED;

  if(!(cli = minnet_client_data(this_val)))
    return JS_UNDEFINED;

  switch(magic) {}
  return ret;
}

static JSValue
minnet_client_setter(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetClient* cli;
  JSValue ret = JS_UNDEFINED;
  if(!(cli = JS_GetOpaque2(ctx, this_val, minnet_client_class_id)))
    return JS_EXCEPTION;

  switch(magic) {}
  return ret;
}

static void
minnet_client_finalizer(JSRuntime* rt, JSValue val) {
  MinnetClient* cli = JS_GetOpaque(val, minnet_client_class_id);
  if(cli) {
    js_free_rt(rt, cli);
  }
  //  JS_FreeValueRT(rt, val);
}

JSClassDef minnet_client_class = {
    "MinnetClient",
    .finalizer = minnet_client_finalizer,
};

const JSCFunctionListEntry minnet_client_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetClient", JS_PROP_CONFIGURABLE),

};

const size_t minnet_client_proto_funcs_size = countof(minnet_client_proto_funcs);

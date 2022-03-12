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
#include <errno.h>

// static MinnetCallback client_cb_message, client_cb_connect, client_cb_close, client_cb_pong, client_cb_fd;

static int client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

// THREAD_LOCAL MinnetClient* minnet_client = 0;

static const struct lws_protocols client_protocols[] = {
    {"http", http_client_callback, 0, 0, 0, 0, 0},
    {"ws", client_callback, 0, 0, 0, 0, 0},
    {"raw", client_callback, 0, 0, 0, 0, 0},
    {0},
};

static void
client_closure_free(void* ptr) {
  struct client_closure* closure = ptr;

  if(--closure->ref_count == 0) {
    if(closure->client) {
      JSContext* ctx = closure->client->context.js;

      printf("%s client=%p\n", __func__, closure->client);

      client_free(closure->client);

      js_free(ctx, closure);
    }
  }
}

struct client_closure*
client_closure_new(JSContext* ctx) {
  return js_mallocz(ctx, sizeof(struct client_closure));
}

struct client_closure*
client_closure_dup(struct client_closure* c) {
  ++c->ref_count;
  return c;
}

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

void
client_free(MinnetClient* client) {
  JSContext* ctx = client->context.js;

  if(--client->ref_count == 0) {
    context_clear(&client->context);

    if(client->connect_info.method)
      js_free(ctx, client->connect_info.method);

    url_free(&client->url, ctx);

    js_free(ctx, client);
  }
}

MinnetClient*
client_dup(MinnetClient* client) {
  ++client->ref_count;
  return client;
}

enum {
  ON_RESOLVE = 0,
  ON_REJECT,
};

static JSValue
minnet_client_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {}

JSValue
minnet_client_closure(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  struct lws_context* lws = 0;
  int argind = 0, status = -1;
  JSValue value, ret = JS_NULL;
  MinnetWebsocket* ws;
  MinnetClient* client = 0;
  struct lws_context_creation_info* info;
  struct lws_client_connect_info* conn;
  JSValue options = argv[0];
  struct lws *wsi = 0, *wsi2;
  const char *tmp = 0, *str;
  BOOL block = TRUE;
  struct wsi_opaque_user_data* opaque = 0;
  char *url, *method_str = 0;

  SETLOG(LLL_INFO)

  if(!(client = js_mallocz(ctx, sizeof(MinnetClient))))
    return JS_ThrowOutOfMemory(ctx);

  if(ptr)
    ((struct client_closure*)ptr)->client = client;

  *client = (MinnetClient){.context = (MinnetContext){.ref_count = 1}, .headers = JS_UNDEFINED, .body = JS_UNDEFINED, .next = JS_UNDEFINED};
  info = &client->context.info;
  conn = &client->connect_info;

  client->context.js = ctx;
  client->context.error = JS_NULL;
  // minnet_client = &client;

  memset(info, 0, sizeof(struct lws_context_creation_info));
  info->options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info->options |= LWS_SERVER_OPTION_H2_JUST_FIX_WINDOW_UPDATE_OVERFLOW;
  info->port = CONTEXT_PORT_NO_LISTEN;
  info->protocols = client_protocols;
  info->user = client;

  if(argc >= 2) {
    MinnetRequest* req;
    if(JS_IsString(argv[argind])) {
      tmp = JS_ToCString(ctx, argv[argind]);
      argind++;
    } else if((req = minnet_request_data(argv[argind]))) {
      client->request = request_dup(req);
      client->url = url_dup(req->url, ctx);
      client->headers = JS_GetPropertyStr(ctx, argv[argind], "headers");
      argind++;
    }
  }

  options = argv[argind];

  if(!JS_IsObject(options))
    return JS_ThrowTypeError(ctx, "argument %d must be options object", argind + 1);

  value = JS_GetPropertyStr(ctx, options, "method");
  str = JS_ToCString(ctx, value);
  method_str = js_strdup(ctx, JS_IsString(value) ? str : method_string(METHOD_GET));
  JS_FreeValue(ctx, value);
  JS_FreeCString(ctx, str);

  if(tmp) {
    url_parse(&client->url, tmp, ctx);
    JS_FreeCString(ctx, tmp);
  } else if(!client->request) {
    url_from(&client->url, options, ctx);
  }

  GETCBPROP(options, "onPong", client->cb.pong)
  GETCBPROP(options, "onClose", client->cb.close)
  GETCBPROP(options, "onConnect", client->cb.connect)
  GETCBPROP(options, "onMessage", client->cb.message)
  GETCBPROP(options, "onFd", client->cb.fd)
  GETCBPROP(options, "onHttp", client->cb.http)

  if(!lws) {
    sslcert_client(ctx, info, options);

    if(!(lws = lws_create_context(info))) {
      lwsl_err("minnet-client: libwebsockets init failed\n");
      return JS_ThrowInternalError(ctx, "minnet-client: libwebsockets init failed");
    }
  }

  JSValue opt_binary = JS_GetPropertyStr(ctx, options, "binary");
  if(!JS_IsUndefined(opt_binary))
    opaque->binary = JS_ToBool(ctx, opt_binary);

  JSValue opt_block = JS_GetPropertyStr(ctx, options, "block");
  if(!JS_IsUndefined(opt_block))
    block = JS_ToBool(ctx, opt_block);

  // url = url_format(&client->url, ctx);
  if(!client->request) {
    client->request = request_new(ctx, url_location(&client->url, ctx), client->url, method_number(method_str));
    client->headers = JS_GetPropertyStr(ctx, options, "headers");
    client->body = JS_GetPropertyStr(ctx, options, "body");
  }

  // headers_from(&client->request->headers, wsi, client->headers, ctx);

  url_info(&client->url, conn);
  conn->pwsi = &wsi;
  conn->context = lws;

#ifdef DEBUG_OUTPUT
  fprintf(stderr, "METHOD: %s\n", method_str);
  fprintf(stderr, "PROTOCOL: %s\n", conn->protocol);
#endif

  switch(protocol_number(client->url.protocol)) {
    case PROTOCOL_HTTP:
    case PROTOCOL_HTTPS: {
      conn->method = method_str;
      break;
    }
  }
  // url_from(&client->url, options, ctx);

  if(!block)
    ret = js_promise_create(ctx, &client->promise);

  // url_dump("url = ", &client->url);

  errno = 0;

  wsi2 = lws_client_connect_via_info(conn);

  /*  fprintf(stderr, "wsi2 = %p, wsi = %p\n", wsi2, wsi);*/

  if(!wsi) {
    if(!block) {
      if(js_promise_pending(&client->promise)) {
        JSValue err = js_error_new(ctx, "[2] Connection failed: %s", strerror(errno));
        js_promise_reject(ctx, &client->promise, err);
        JS_FreeValue(ctx, err);
      }
    } else {
      ret = JS_ThrowInternalError(ctx, "Connection failed: %s", strerror(errno));
      goto fail;
    }
  }

  if(!block)
    return ret;

  // minnet_exception = FALSE;
  opaque = lws_opaque(wsi, client->context.js);

  for(;;) {
    if(status != opaque->status) {
      status = opaque->status;
      // fprintf(stderr, "STATUS: %s\n", ((const char*[]){"CONNECTING", "OPEN", "CLOSING", "CLOSED"})[status]);
    }

    if(status == CLOSED)
      break;

    js_std_loop(ctx);

    if(client->context.exception) {
      ret = JS_EXCEPTION;
      break;
    }
  }

  // client_free(client);

  // minnet_client = NULL;
fail:
  return ret;
}

JSValue
minnet_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct client_closure* closure;
  JSValue func[2], ret, tmp;

  if(!(closure = client_closure_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  ret = minnet_client_closure(ctx, this_val, argc, argv, 0, closure);

  // closure->client->context.ref_count += 2;

  func[0] = JS_NewCClosure(ctx, &minnet_client_handler, 1, ON_RESOLVE, client_closure_dup(closure), client_closure_free);
  func[1] = JS_NewCClosure(ctx, &minnet_client_handler, 1, ON_REJECT, client_closure_dup(closure), client_closure_free);

  tmp = js_invoke(ctx, ret, "then", 1, &func[0]);
  JS_FreeValue(ctx, ret);
  ret = tmp;

  tmp = js_invoke(ctx, ret, "catch", 1, &func[1]);
  JS_FreeValue(ctx, ret);
  ret = tmp;

  JS_FreeValue(ctx, func[0]);
  JS_FreeValue(ctx, func[1]);

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
  MinnetClient* client = lws_client(wsi);
  MinnetSession* sess = &client->session;
  JSContext* ctx;
  struct wsi_opaque_user_data* opaque;
  int n;

  /*  if(sess)
      client = sess->client ? sess->client : (sess->client = lws_client(wsi));
    else
      client */

  ctx = client->context.js;
  opaque = lws_opaque(wsi, ctx);

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
            (reason == LWS_CALLBACK_RAW_RX || reason == LWS_CALLBACK_CLIENT_RECEIVE || reason == LWS_CALLBACK_RECEIVE) ? 0 : (char*)in);

  switch(reason) {
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
    case LWS_CALLBACK_PROTOCOL_INIT: {
      return 0;
    }
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: {
      return 0;
    }
    case LWS_CALLBACK_WS_CLIENT_BIND_PROTOCOL:
    case LWS_CALLBACK_RAW_SKT_BIND_PROTOCOL: {
      sess->req_obj = JS_NULL;
      sess->resp_obj = JS_NULL;
      break;
    }
    case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL:
    case LWS_CALLBACK_RAW_SKT_DROP_PROTOCOL: {
      BOOL is_error = JS_IsUndefined(client->context.error);

      (is_error ? js_promise_reject : js_promise_resolve)(ctx, &client->promise, client->context.error);
      JS_FreeValue(ctx, client->context.error);
      client->context.error = JS_UNDEFINED;
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
        int32_t result = -1;
        if(reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR && in)
          client->context.error = JS_NewStringLen(ctx, in, len);
        else
          client->context.error = JS_UNDEFINED;

        opaque->status = CLOSING;
        if((client->cb.close.ctx = ctx)) {
          JSValue ret;
          int argc, err = opaque ? opaque->error : 0;
          JSValueConst cb_argv[4] = {sess->ws_obj};

          if(reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR) {
            argc = 2;
            cb_argv[1] = JS_DupValue(ctx, client->context.error);
          } else {
            argc = 4;
            cb_argv[1] = close_status(ctx, in, len);
            cb_argv[2] = close_reason(ctx, in, len);
            cb_argv[3] = JS_NewInt32(ctx, err);
          }

          ret = minnet_emit(&client->cb.close, argc, cb_argv);
          if(!client_exception(client, ret))
            if(JS_IsNumber(ret))
              JS_ToInt32(ctx, &result, ret);
          JS_FreeValue(ctx, ret);
          JS_FreeValue(ctx, cb_argv[1]);
          JS_FreeValue(ctx, cb_argv[2]);
          JS_FreeValue(ctx, cb_argv[3]);
        }
        return result;
      }
    }

    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
    case LWS_CALLBACK_RAW_CONNECTED: {
      if(opaque->status < OPEN) {
        JSContext* ctx;
        int status;
        status = lws_http_client_http_response(wsi);

        opaque->status = OPEN;
        if((ctx = client->cb.connect.ctx)) {

          // opaque->ws = ws_new(wsi, ctx);

          sess->ws_obj = minnet_ws_wrap(ctx, wsi);

          if(reason != LWS_CALLBACK_RAW_CONNECTED) {
            sess->req_obj = minnet_request_wrap(ctx, client->request);

            /* sess->resp_obj = minnet_response_new(ctx, client->request->url, status, TRUE, "text/html");

            client->response = minnet_response_data(sess->resp_obj);*/
          }
          // lwsl_user("client   " FGC(171, "%-38s") " fd=%i, in=%.*s\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);

          if((client->cb.connect.ctx = ctx))
            client_exception(client, minnet_emit(&client->cb.connect, 3, &sess->ws_obj));
        }
        /*if(!minnet_response_data(sess->resp_obj))*/
      }
      break;
    }

    case LWS_CALLBACK_CLIENT_WRITEABLE:
    case LWS_CALLBACK_RAW_WRITEABLE: {
      MinnetBuffer* buf = &sess->send_buf;
      int ret, size = buffer_BYTES(buf);

      if((ret = lws_write(wsi, buf->read, size, LWS_WRITE_TEXT)) != size) {
        lwsl_err("sending message failed: %d < %d\n", ret, size);
        return -1;
      }

      buffer_reset(buf);
      break;
    }

    case LWS_CALLBACK_RECEIVE:
    case LWS_CALLBACK_CLIENT_RECEIVE:
    case LWS_CALLBACK_RAW_RX: {
      if((client->cb.message.ctx = ctx)) {
        MinnetWebsocket* ws = minnet_ws_data(sess->ws_obj);
        JSValue msg = opaque->binary ? JS_NewArrayBufferCopy(ctx, in, len) : JS_NewStringLen(ctx, in, len);
        JSValue cb_argv[] = {sess->ws_obj, msg};

        client_exception(client, minnet_emit(&client->cb.message, countof(cb_argv), cb_argv));

        JS_FreeValue(ctx, cb_argv[1]);
      }
      break;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
      if((client->cb.pong.ctx = ctx)) {
        JSValue data = JS_NewArrayBufferCopy(client->cb.pong.ctx, in, len);
        JSValue cb_argv[] = {sess->ws_obj, data};
        client_exception(client, minnet_emit(&client->cb.pong, 2, cb_argv));
        JS_FreeValue(ctx, cb_argv[1]);
      }
      break;
    }

    default: {
      minnet_lws_unhandled(__func__, reason);
      break;
    }
  }

  if(opaque && opaque->status >= CLOSING)
    return -1;

  return 0;
}

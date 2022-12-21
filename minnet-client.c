#define _GNU_SOURCE
#include "minnet-client-http.h"
#include "minnet-client.h"
#include "minnet-websocket.h"
#include "minnet-request.h"
#include "minnet-response.h"
#include "context.h"
#include "closure.h"
#include "minnet.h"
#include "jsutils.h"
#include <quickjs-libc.h>
#include <strings.h>
#include <errno.h>
#include <libwebsockets.h>

// static JSCallback client_cb_message, client_cb_connect, client_cb_close, client_cb_pong, client_cb_fd;

static int client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

static /*THREAD_LOCAL*/ struct list_head minnet_clients = {0, 0};

static const struct lws_protocols client_protocols[] = {
    {"raw", client_callback, 0, 0, 0, 0, 0},
    {"http", http_client_callback, 0, 0, 0, 0, 0},
    {"ws", client_callback, 0, 0, 0, 0, 0},
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

void
client_certificate(struct context* context, JSValueConst options) {
  struct lws_context_creation_info* info = &context->info;
  JSContext* ctx = context->js;

  context->crt = JS_GetPropertyStr(ctx, options, "sslCert");
  context->key = JS_GetPropertyStr(ctx, options, "sslPrivateKey");
  context->ca = JS_GetPropertyStr(ctx, options, "sslCA");

  if(JS_IsString(context->crt))
    info->client_ssl_cert_filepath = js_tostring(ctx, context->crt);
  else if(JS_IsObject(context->crt))
    info->client_ssl_cert_mem = js_toptrsize(ctx, &info->client_ssl_cert_mem_len, context->crt);

  if(JS_IsString(context->key))
    info->client_ssl_private_key_filepath = js_tostring(ctx, context->key);
  else if(JS_IsObject(context->key))
    info->client_ssl_key_mem = js_toptrsize(ctx, &info->client_ssl_key_mem_len, context->key);

  if(JS_IsString(context->ca))
    info->client_ssl_ca_filepath = js_tostring(ctx, context->ca);
  else if(JS_IsObject(context->ca))
    info->client_ssl_ca_mem = js_toptrsize(ctx, &info->client_ssl_ca_mem_len, context->ca);
}

MinnetClient*
client_new(JSContext* ctx) {
  MinnetClient* client;

  if(!(client = js_mallocz(ctx, sizeof(MinnetClient))))
    return 0;

  client_zero(client);

  if(minnet_clients.next == NULL)
    init_list_head(&minnet_clients);

  list_add_tail(&client->link, &minnet_clients);

  return client;
}

MinnetClient*
client_find(struct lws* wsi) {
  struct list_head* el;

  list_for_each_prev(el, &minnet_clients) {
    MinnetClient* client = list_entry(el, MinnetClient, link);

    if(client->wsi == wsi)
      return client;
  }
  return 0;
}

void
client_free(MinnetClient* client, JSContext* ctx) {
  if(--client->ref_count == 0) {
    DEBUG("%s() client=%p\n", __func__, client);

    if(client->link.prev)
      list_del(&client->link);

    JS_FreeValue(ctx, client->headers);
    client->headers = JS_UNDEFINED;
    JS_FreeValue(ctx, client->body);
    client->body = JS_UNDEFINED;
    JS_FreeValue(ctx, client->next);
    client->next = JS_UNDEFINED;

    if(client->connect_info.method) {
      js_free(ctx, (void*)client->connect_info.method);
      client->connect_info.method = 0;
    }

    js_promise_free(ctx, &client->promise);

    context_clear(&client->context);
    session_clear(&client->session, ctx);

    js_free(ctx, client);
  }
}

void
client_free_rt(MinnetClient* client, JSRuntime* rt) {
  if(--client->ref_count == 0) {
    DEBUG("%s() client=%p\n", __func__, client);

    if(client->link.prev)
      list_del(&client->link);

    JS_FreeValueRT(rt, client->headers);
    client->headers = JS_UNDEFINED;
    JS_FreeValueRT(rt, client->body);
    client->body = JS_UNDEFINED;
    JS_FreeValueRT(rt, client->next);
    client->next = JS_UNDEFINED;

    if(client->connect_info.method) {
      js_free_rt(rt, (void*)client->connect_info.method);
      client->connect_info.method = 0;
    }

    js_promise_free_rt(rt, &client->promise);

    context_clear(&client->context);
    session_clear_rt(&client->session, rt);

    js_free_rt(rt, client);
  }
}

void
client_zero(MinnetClient* client) {
  // memset(client, 0, sizeof(MinnetClient));
  client->ref_count = 1;
  client->headers = JS_NULL;
  client->body = JS_NULL;
  client->next = JS_NULL;

  session_zero(&client->session);
  js_promise_zero(&client->promise);
  callbacks_zero(&client->on);
  // client->link.next = client->link.prev = 0;
}

MinnetClient*
client_dup(MinnetClient* client) {
  ++client->ref_count;
  return client;
}

struct client_context*
lws_client(struct lws* wsi) {
  return lws_context_user(lws_get_context(wsi));
}

static JSValue
minnet_client_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {

  /*switch(magic) {
    case ON_RESOLVE: {
      DEBUG("%s %s %d\n", __func__, "ON_RESOLVE", ((union closure*)ptr)->ref_count);
      break;
    }
    case ON_REJECT: {
      DEBUG("%s %s\n", __func__, "ON_REJECT", ((union closure*)ptr)->ref_count);
      break;
    }
  }
*/
  return JS_UNDEFINED;
}

JSValue
minnet_client_closure(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  int argind = 0, status = -1;
  JSValue value, ret = JS_NULL;
  MinnetClient* client = 0;
  JSValue options = argv[0];
  struct lws /**wsi = 0, */* wsi2;

  BOOL block = TRUE, binary = FALSE;
  struct wsi_opaque_user_data* opaque = 0;
  MinnetProtocol proto;

  // SETLOG(LLL_INFO)

  if(!(client = client_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  client_zero(client);

  if(ptr) {
    union closure* closure = ptr;

    closure->pointer = client;
    closure->free_func = (closure_free_t*)client_free_rt;
  }

  *client = (MinnetClient){
      .context = (struct context){.ref_count = 1},
      .headers = JS_UNDEFINED,
      .body = JS_UNDEFINED,
      .next = JS_UNDEFINED,
  };

  session_zero(&client->session);

  client->request = request_from(argc, argv, ctx);
  enum protocol p = url_protocol(client->request->url);
  client->request->secure = protocol_is_tls(p);

  js_promise_zero(&client->promise);

  if(argc >= 2) {

    if(JS_IsObject(argv[1]))
      argind++;
    /*    if(JS_IsString(argv[argind])) {
          tmp = JS_ToCString(ctx, argv[argind]);
          argind++;
        } else if((req = minnet_request_data(argv[argind]))) {
          client->request = request_dup(req);
          client->url = url_clone(req->url, ctx);
          client->headers = JS_GetPropertyStr(ctx, argv[argind], "headers");
          argind++;
        }*/
  }

  options = argv[argind];

  if(!JS_IsObject(options))
    return JS_ThrowTypeError(ctx, "argument %d must be options object", argind + 1);

  {
    struct context* context = &client->context;

    context->js = ctx;
    context->error = JS_NULL;

    memset(&context->info, 0, sizeof(struct lws_context_creation_info));
    context->info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    context->info.options |= LWS_SERVER_OPTION_H2_JUST_FIX_WINDOW_UPDATE_OVERFLOW;
    context->info.port = CONTEXT_PORT_NO_LISTEN;
    context->info.protocols = client_protocols;
    context->info.user = client;

    if(!context->lws) {
      //  client_certificate(&client->context, options);

      if(!(context->lws = lws_create_context(&context->info))) {
        lwsl_err("minnet-client: libwebsockets init failed\n");
        return JS_ThrowInternalError(ctx, "minnet-client: libwebsockets init failed");
      }
    }
  }

  GETCBPROP(options, "onPong", client->on.pong)
  GETCBPROP(options, "onClose", client->on.close)
  GETCBPROP(options, "onConnect", client->on.connect)
  GETCBPROP(options, "onMessage", client->on.message)
  GETCBPROP(options, "onFd", client->on.fd)
  GETCBPROP(options, "onHttp", client->on.http)

  value = JS_GetPropertyStr(ctx, options, "binary");
  if(!JS_IsUndefined(value))
    binary = JS_ToBool(ctx, value);
  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, options, "block");
  if(!JS_IsUndefined(value))
    block = JS_ToBool(ctx, value);
  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, options, "body");
  if(!JS_IsUndefined(value))
    client->body = JS_DupValue(ctx, value);
  JS_FreeValue(ctx, value);

  /* value = JS_GetPropertyStr(ctx, options, "headers");
   if(JS_IsObject(value)) {
     client->headers = JS_DupValue(ctx, value);
   }
   JS_FreeValue(ctx, value);

   if(JS_IsObject(client->headers)) {
     headers_fromobj(&client->request->headers, client->headers, ctx);
   }
 */
  client->response = response_new(ctx);
  url_copy(&client->response->url, client->request->url, ctx);
  client->response->generator = generator_new(ctx);

  proto = protocol_number(client->request->url.protocol);

  url_info(client->request->url, &client->connect_info);

  DEBUG("alpn = '%s'\n", client->connect_info.alpn);

  client->connect_info.pwsi = &client->wsi;
  client->connect_info.context = client->context.lws;

  switch(proto) {
    case PROTOCOL_RAW: {
      client->connect_info.method = js_strdup(ctx, "RAW");
      break;
    }
    case PROTOCOL_HTTP:
    case PROTOCOL_HTTPS: {
      const char* str;

      value = JS_GetPropertyStr(ctx, options, "method");
      str = JS_ToCString(ctx, value);
      client->connect_info.method = js_strdup(ctx, JS_IsString(value) ? str : method_string(METHOD_GET));
      JS_FreeValue(ctx, value);
      JS_FreeCString(ctx, str);

      break;
    }
    default: {
      break;
    }
  }

  DEBUG("METHOD: %s\n", client->connect_info.method);
  DEBUG("PROTOCOL: %s\n", client->connect_info.protocol);

  if(!block)
    ret = js_promise_create(ctx, &client->promise);

  errno = 0;

#ifdef LWS_WITH_UDP
  if(proto == PROTOCOL_RAW && !strcmp(client->request->url.protocol, "udp")) {
    struct lws_vhost* vhost;
    MinnetURL* url = &client->request->url;

    vhost = lws_create_vhost(client->context.lws, &client->context.info);
    wsi2 = lws_create_adopt_udp(vhost, url->host, url->port, 0, "raw", 0, 0, 0, 0, 0);
    *client->connect_info.pwsi = wsi2;
  } else
#endif
  {
    wsi2 = lws_client_connect_via_info(&client->connect_info);
  }
  DEBUG("client->wsi = %p, wsi2 = %p, h2 = %d, ssl = %d\n", client->wsi, wsi2, wsi_http2(client->wsi), wsi_tls(client->wsi));

  if(!client->wsi /*&& !wsi2*/) {
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

  opaque = lws_opaque(client->wsi, client->context.js);
  opaque->binary = binary;
  /*opaque->req = client->request;
  opaque->resp = client->response;*/

  if(!block)
    return ret;

  for(;;) {
    if(status != opaque->status)
      status = opaque->status;

    if(status == CLOSED)
      break;

    js_std_loop(ctx);

    if(client->context.exception) {
      ret = JS_EXCEPTION;
      break;
    }
  }

  // client_free(client);

fail:
  return ret;
}

JSValue
minnet_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  union closure* closure;
  JSValue ret;

  if(!(closure = closure_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  ret = minnet_client_closure(ctx, this_val, argc, argv, 0, closure);

  if(js_is_promise(ctx, ret)) {
    JSValue func[2], tmp;

    func[0] = JS_NewCClosure(ctx, &minnet_client_handler, 1, ON_RESOLVE, closure_dup(closure), closure_free);
    func[1] = JS_NewCClosure(ctx, &minnet_client_handler, 1, ON_REJECT, closure_dup(closure), closure_free);

    tmp = js_invoke(ctx, ret, "then", 1, &func[0]);
    JS_FreeValue(ctx, ret);
    ret = tmp;

    tmp = js_invoke(ctx, ret, "catch", 1, &func[1]);
    JS_FreeValue(ctx, ret);
    ret = tmp;

    JS_FreeValue(ctx, func[0]);
    JS_FreeValue(ctx, func[1]);
  }

  closure_free(closure);

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
  MinnetClient* client = lws_client(wsi);
  struct wsi_opaque_user_data* opaque = 0;
  JSContext* ctx;
  int ret = 0;

  if(lws_reason_poll(reason))
    return wsi_handle_poll(wsi, reason, &client->on.fd, in);

  if(lws_reason_http(reason))
    return http_client_callback(wsi, reason, user, in, len);

  if((ctx = client->context.js))
    opaque = lws_opaque(wsi, ctx);

  LOGCB("CLIENT      ", "fd=%d, h2=%i, tls=%i%s%.*s%s", lws_get_socket_fd(wsi), wsi_http2(wsi), wsi_tls(wsi), (in && len) ? ", in='" : "", (int)len, (char*)in, (in && len) ? "'" : "");

  switch(reason) {
    case LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION: {
      return 0;
    }
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
    case LWS_CALLBACK_PROTOCOL_INIT: {
      break;
    }
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: {
      break;
    }
    case LWS_CALLBACK_WS_CLIENT_BIND_PROTOCOL:
    case LWS_CALLBACK_RAW_SKT_BIND_PROTOCOL: {
      session_zero(&client->session);
      break;
    }
    case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL:
    case LWS_CALLBACK_RAW_SKT_DROP_PROTOCOL: {

      break;
    }

    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
    case LWS_CALLBACK_CONNECTING: {
      break;
    }
    case LWS_CALLBACK_WSI_CREATE:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: {
      if(!opaque->ws)
        opaque->ws = ws_new(wsi, ctx);
      break;
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
        JSContext* ctx;
        int32_t result = -1, err = /*opaque ? opaque->error :*/ 0;

        if(reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR && in) {
          if(!strncmp("conn fail: ", in, 11)) {
            err = /*opaque->error =*/atoi(&((const char*)in)[11]);
            client->context.error = JS_NewString(client->context.js, strerror(err));
          } else {
            client->context.error = JS_NewStringLen(client->context.js, in, len);
          }
        } else
          client->context.error = JS_UNDEFINED;

        opaque->status = CLOSING;
        if((ctx = client->on.close.ctx)) {
          JSValue ret;
          int argc = 1;
          JSValueConst cb_argv[4] = {client->session.ws_obj};

          if(reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR) {

            cb_argv[argc++] = JS_UNDEFINED;
            cb_argv[argc++] = JS_DupValue(ctx, client->context.error);
            cb_argv[argc++] = JS_NewInt32(ctx, err);

          } else {
            cb_argv[argc++] = close_status(ctx, in, len);
            cb_argv[argc++] = close_reason(ctx, in, len);
            cb_argv[argc++] = JS_NewInt32(ctx, err);
          }

          ret = client_exception(client, callback_emit(&client->on.close, argc, cb_argv));
          if(JS_IsNumber(ret))
            JS_ToInt32(ctx, &result, ret);
          JS_FreeValue(ctx, ret);

          while(--argc >= 0) JS_FreeValue(ctx, cb_argv[argc]);
        }
        ret = result;
        break;
      }
    }
    case LWS_CALLBACK_RAW_ADOPT: {
      // lwsl_user("ADOPT! %s", client->request->url.protocol);

      if(strcasecmp(client->request->url.protocol, "udp"))
        break;
    }

    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
    case LWS_CALLBACK_RAW_CONNECTED: {
      if(opaque->status < OPEN) {
        JSContext* ctx;

        /* int status;
         status = lws_http_client_http_response(wsi);*/

        opaque->status = OPEN;
        if((ctx = client->on.connect.ctx)) {
          client->request->ip = wsi_ipaddr(wsi, ctx);
          client->session.ws_obj = minnet_ws_fromwsi(ctx, wsi);

          if(reason != LWS_CALLBACK_RAW_CONNECTED) {
            client->session.req_obj = minnet_request_wrap(ctx, client->request);
          }
          // lwsl_user("client   " FGC(171, "%-38s") " fd=%i, in=%.*s\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);

          if((client->on.connect.ctx = ctx))
            client_exception(client, callback_emit(&client->on.connect, 3, &client->session.ws_obj));
        }
        /*if(!minnet_response_data(sess->resp_obj))*/
      }
      break;
    }

    case LWS_CALLBACK_CLIENT_WRITEABLE:
    case LWS_CALLBACK_RAW_WRITEABLE: {

      /*
      ByteBuffer* buf = &client->session.send_buf;
      int ret, size = buffer_REMAIN(buf);

      if((ret = lws_write(wsi, buf->read, size, LWS_WRITE_TEXT)) != size) {
        lwsl_err("sending message failed: %d < %d\n", ret, size);
        ret = -1;
        break;
      }

      buffer_reset(buf);*/

      session_writable(&client->session, opaque->binary, ctx);
      break;
    }

    case LWS_CALLBACK_RECEIVE:
    case LWS_CALLBACK_CLIENT_RECEIVE:
    case LWS_CALLBACK_RAW_RX: {
      JSContext* ctx;
      if((ctx = client->on.message.ctx)) {
        // MinnetWebsocket* ws = minnet_ws_data(client->session.ws_obj);
        JSValue msg = opaque->binary ? JS_NewArrayBufferCopy(ctx, in, len) : JS_NewStringLen(ctx, in, len);
        JSValue cb_argv[] = {client->session.ws_obj, msg};

        client_exception(client, callback_emit(&client->on.message, countof(cb_argv), cb_argv));

        JS_FreeValue(ctx, cb_argv[1]);
      }
      break;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
      JSContext* ctx;
      if((ctx = client->on.pong.ctx)) {
        JSValue data = JS_NewArrayBufferCopy(ctx, in, len);
        JSValue cb_argv[] = {client->session.ws_obj, data};
        client_exception(client, callback_emit(&client->on.pong, 2, cb_argv));
        JS_FreeValue(ctx, cb_argv[1]);
      }
      break;
    }
    case LWS_CALLBACK_WSI_DESTROY: {
      if(client->wsi == wsi) {
        BOOL is_error = JS_IsUndefined(client->context.error);

        (is_error ? js_promise_reject : js_promise_resolve)(client->context.js, &client->promise, client->context.error);
        JS_FreeValue(client->context.js, client->context.error);
        client->context.error = JS_UNDEFINED;
      }
      break;
    }
    case LWS_CALLBACK_PROTOCOL_DESTROY: {
      break;
    }
    default: {
      minnet_lws_unhandled(__func__, reason);
      break;
    }
  }

  if(opaque && opaque->status >= CLOSING)
    ret = -1;

  /*  lwsl_user(len ? "client      " FG("%d") "%-38s" NC " is_ssl=%i len=%zu in='%.*s' ret=%d\n" : "client      " FG("%d") "%-38s" NC " is_ssl=%i len=%zu\n",
              22 + (reason * 2),
              lws_callback_name(reason) + 13,
              wsi_tls(wsi),
              len,
              (int)MIN(len, 32),
              (reason == LWS_CALLBACK_RAW_RX || reason == LWS_CALLBACK_CLIENT_RECEIVE || reason == LWS_CALLBACK_RECEIVE) ? 0 : (char*)in,
              ret);*/

  return ret;
}

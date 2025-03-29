#include "minnet-client.h"
#include "minnet-client-http.h"
#include "minnet-websocket.h"
#include "minnet-response.h"
#include "minnet.h"
#include "headers.h"
#include "js-utils.h"
#include "assure.h"
#include <libwebsockets.h>

static int
http_client_error(MinnetClient* cli, void* in, size_t len, struct session_data* session, struct wsi_opaque_user_data* opaque, JSContext* ctx) {
  if(js_async_pending(&cli->promise)) {
    JSValue err = js_error_new(ctx, "%s", (char*)in);

    js_async_reject(ctx, &cli->promise, err);
    JS_FreeValue(ctx, err);
  }

  if(callback_valid(&cli->on.close)) {
    JSValueConst argv[] = {
        opaque->ws ? JS_DupValue(ctx, session->ws_obj) : JS_NewInt32(ctx, opaque->fd),
        js_error_new(ctx, "%s", (char*)in),
    };

    minnet_client_exception(cli, callback_emit(&cli->on.close, countof(argv), argv));

    JS_FreeValue(ctx, argv[0]);
    JS_FreeValue(ctx, argv[1]);
  }

  return -1;
}

static int
http_client_established(MinnetClient* cli, struct lws* wsi, JSContext* ctx) {
  int ret = 0;
  struct wsi_opaque_user_data* opaque = opaque_from_wsi(wsi, ctx);
  MinnetResponse* resp;
  char* type;

  assure(minnet_client_lws(cli) == wsi);

  if(strcmp(lws_get_protocol(wsi)->name, "ws"))
    opaque->status = OPEN;

  // cli->req->h2 = wsi_http2(wsi);

  if(!(resp = opaque->resp)) {
    resp = opaque->resp = response_new(ctx);
    resp->status = lws_http_client_http_response(wsi);

    headers_tobuffer(ctx, &opaque->resp->headers, wsi);

    cli->session.resp_obj = minnet_response_wrap(ctx, opaque->resp);
  }

  if((type = response_type(resp, ctx))) {
    if(!strncmp(type, "text/", 5)){
      response_generator(resp,ctx);
      resp->body->block_fn = &block_tostring;
    }

    js_free(ctx, type);
  }

  url_copy(&resp->url, cli->request->url, ctx);

  // opaque->resp->headers = headers_gettoken(ctx, wsi, WSI_TOKEN_HTTP_CONTENT_TYPE);

  if(!opaque->ws)
    opaque->ws = ws_new(wsi, ctx);

  cli->session.ws_obj = minnet_ws_wrap(ctx, opaque->ws);

  {
    size_t i, hdrlen = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP);
    char buf[(((hdrlen + 1) + 7) >> 3) << 3];

    lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_HTTP);
    buf[hdrlen] = '\0';

    if(buf[(i = byte_chr(buf, hdrlen, ' '))])
      i += 1;

    cli->response->status_text = js_strdup(ctx, &buf[i]);
  }

  if(js_async_pending(&cli->promise)) {
    JSValue client = minnet_client_wrap(ctx, minnet_client_dup(cli));

    js_async_resolve(ctx, &cli->promise, client);

    JS_FreeValue(ctx, client);
  }

  if(callback_valid(&cli->on.http)) {
    JSValue retval = minnet_client_exception(cli, callback_emit_this(&cli->on.http, cli->session.ws_obj, 2, &cli->session.req_obj));

    if(!js_is_nullish(retval)) {
      BOOL terminate = JS_ToBool(ctx, retval);

      if(terminate)
        cli->lwsret = 1;
    }

    JS_FreeValue(cli->on.http.ctx, retval);
  }

  if(resp->status >= 400) {
    // generator_continuous(resp->body, JS_NULL);
    // lws_set_timeout(wsi, 1, LWS_TO_KILL_ASYNC);
    lws_wsi_close(wsi, LWS_TO_KILL_SYNC);
  }

  if(method_number(cli->connect_info.method) == METHOD_POST) {
    lws_client_http_body_pending(wsi, 1);
    lws_callback_on_writable(wsi);
  }

  return ret;
}

static int
http_client_writable(MinnetClient* cli, struct lws* wsi, JSContext* ctx) {

  assure(minnet_client_lws(cli) == wsi);

  if(callback_valid(&cli->on.writeable)) {
    JSValue ret;
    struct wsi_opaque_user_data* opaque = opaque_from_wsi(wsi, ctx);

    opaque->writable = TRUE;
    ret = minnet_client_exception(cli, callback_emit(&cli->on.writeable, 1, &cli->session.ws_obj));

    // if(JS_IsBool(ret))
    if(JS_ToBool(cli->on.writeable.ctx, ret) == FALSE)
      cli->on.writeable = CALLBACK_INIT(0, JS_NULL, JS_NULL);

    opaque->writable = FALSE;

    if(callback_valid(&cli->on.writeable))
      lws_callback_on_writable(wsi);

    return 0;
  }

  if(method_number(cli->connect_info.method) == METHOD_POST) {
    BOOL done = FALSE;
    JSValue value;
    int n;
    ssize_t size, r;
    ByteBuffer buf;

    buffer_alloc(&buf, 1024);

    if(lws_http_is_redirected_to_get(wsi))
      return 0;

    if(JS_IsObject(cli->body)) {
      while(!done) {
        value = js_iterator_next(ctx, cli->body, &cli->next, &done, 0, 0);

#ifdef DEBUG_OUTPUT
        lwsl_user("DEBUG %-22s js_iterator_next() = %s %i done=%i", __func__, JS_ToCString(ctx, value), JS_VALUE_GET_TAG(value), done);
#endif

        if(JS_IsException(value)) {
          JSValue exception = JS_GetException(ctx);
          js_error_print(ctx, exception);
          JS_Throw(ctx, exception);

        } else if(!js_is_nullish(value)) {
          JSBuffer input = js_buffer_new(ctx, value);
          // js_std_dump_error(ctx);

#ifdef DEBUG_OUTPUT
          lwsl_user("DEBUG %-22s \x1b[2K\ryielded %p %zu", __func__, input.data, input.size);
#endif

          buffer_append(&buf, input.data, input.size);
#ifdef DEBUG_OUTPUT
          lwsl_user("DEBUG %-22s \x1b[2K\rbuffered %zu/%zu bytes", __func__, buffer_REMAIN(&buf), buffer_HEAD(&buf));
#endif

          js_buffer_free(&input, JS_GetRuntime(ctx));
        }

        break;
      }

    } else if(!js_is_nullish(cli->body)) {
      buffer_fromvalue(&buf, cli->body, ctx);
      done = TRUE;
    }

    n = done ? LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP;
    size = buf.write - buf.start;

    if((r = lws_write(wsi, buf.start, size, (enum lws_write_protocol)n)) != size)
      return 1;

#ifdef DEBUG_OUTPUT
    lwsl_user("DEBUG %-22s \x1b[2K\rwrote %zd%s", __func__, r, n == LWS_WRITE_HTTP_FINAL ? " (final)" : "");
#endif

    if(n != LWS_WRITE_HTTP_FINAL)
      lws_callback_on_writable(wsi);
    else
      lws_client_http_body_pending(wsi, 0);
  }

  return 0;
}

static int
http_client_completed(MinnetClient* cli, struct lws* wsi, struct wsi_opaque_user_data* opaque) {
  int32_t r32 = -1;

  assure(minnet_client_lws(cli) == wsi);

  if(opaque->resp->body)
    generator_finish(opaque->resp->body);

  if(callback_valid(&cli->on.http)) {
    MinnetRequest* req;
    MinnetResponse* resp = opaque->resp;

    // url_copy(&resp->url, cli->request->url, cli->on.http.ctx);
    // resp->type = headers_get(&resp->headers, "content-type", cli->on.http.ctx);

    JSValue ret = minnet_client_exception(cli, callback_emit_this(&cli->on.http, cli->session.ws_obj, 2, &cli->session.req_obj));

    if(JS_IsNumber(ret)) {
      JS_ToInt32(cli->on.http.ctx, &r32, ret);

      // printf("onRequest() returned: %" PRId32 "\n", r32);
      cli->wsi = wsi;

    } else if((req = minnet_request_data(ret))) {
      url_info(req->url, &cli->connect_info);
      cli->connect_info.pwsi = &cli->wsi;
      cli->connect_info.context = cli->context.lws;

      if(cli->request) {
        request_free(cli->request, JS_GetRuntime(cli->on.http.ctx));
        cli->request = 0;
      }

      if(cli->response) {
        response_free(cli->response, JS_GetRuntime(cli->on.http.ctx));
        cli->response = 0;
      }
      if(opaque->resp) {
        response_free(opaque->resp, JS_GetRuntime(cli->on.http.ctx));
        opaque->resp = 0;
      }

      cli->request = req;
      cli->response = response_new(cli->on.http.ctx);

      lws_client_connect_via_info(&cli->connect_info);

      r32 = 0;
    } /*else if(js_is_promise(ctx, ret)) {
      JSValue promise = client_promise(ctx, session, resp, wsi, ret);
    }*/
    else {
      const char* str = JS_ToCString(cli->on.http.ctx, ret);

      JS_ThrowInternalError(cli->on.http.ctx, "onRequest didn't return a number: %s", str);

      if(str)
        JS_FreeCString(cli->on.http.ctx, str);
    }

    if(r32 != 0)
      lws_cancel_service(lws_get_context(wsi));

    JS_FreeValue(cli->on.http.ctx, ret);

    return r32;
  }

  return cli->lwsret;
}

int
minnet_http_client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetClient* client = lws_client(wsi);
  struct session_data* session = &client->session;
  JSContext* ctx = client ? client->context.js : 0;
  struct wsi_opaque_user_data* opaque;

  if(reason == LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS)
    return 0;

  if(lws_reason_poll(reason))
    return minnet_pollfds_change(wsi, reason, &client->on.fd, in);

  if((opaque = opaque_from_wsi(wsi, ctx)) && !opaque->sess && session)
    opaque->sess = session;

  if(reason != LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ)
    LOGCB("CLIENT-HTTP ",
          "fd=%d, h2=%i, tls=%i%s%.*s%s",
          lws_get_socket_fd(lws_get_network_wsi(wsi)),
          wsi_http2(wsi),
          wsi_tls(wsi),
          (in && len) ? ", in='" : "",
          (int)len,
          (char*)in,
          (in && len) ? "'" : "");

  switch(reason) {
    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH: {
      /*MinnetRequest* req = client->request;
        MinnetResponse* resp;

        if(req)
          session->req_obj = minnet_request_wrap(ctx, client->request);*/

      /*if(!(resp = opaque->resp)) {
        resp = opaque->resp = response_new(ctx);

        resp->body = generator_new(ctx);
        resp->status = lws_http_client_http_response(wsi);

        headers_tobuffer(ctx, &opaque->resp->headers, wsi);
        session->resp_obj = minnet_response_wrap(ctx, opaque->resp);
      }*/

      return 0;
    }

    case LWS_CALLBACK_VHOST_CERT_AGING:
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
      return 0;
    }

    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_WSI_CREATE: {
      if(opaque)
        opaque->fd = lws_get_socket_fd(lws_get_network_wsi(wsi));

      break;
    }

    case LWS_CALLBACK_CONNECTING: {
      break;
    }

    case LWS_CALLBACK_PROTOCOL_INIT: {
      return 0;
    }

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      return http_client_error(client, in, len, session, opaque, ctx);
    }

    case LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL: {
      if(opaque) {
        if(opaque->fd != -1) {
          if(callback_valid(&client->on.close)) {
            JSValueConst argv[] = {
                opaque->ws ? JS_DupValue(client->on.close.ctx, session->ws_obj) : JS_NewInt32(client->on.close.ctx, opaque->fd),
                JS_NewInt32(client->on.close.ctx, 0),
            };

            minnet_client_exception(client, callback_emit(&client->on.close, countof(argv), argv));

            JS_FreeValue(client->on.close.ctx, argv[0]);
            JS_FreeValue(client->on.close.ctx, argv[1]);
          }
        }
      }

      session_init(session, wsi_context(wsi));

      session->req_obj = JS_NULL;
      session->resp_obj = JS_NULL;

      break;
    }

    case LWS_CALLBACK_CLIENT_HTTP_DROP_PROTOCOL: {
      break;
    }

    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
      MinnetRequest* req = client->request;
      ByteBuffer buf = BUFFER_N(*(uint8_t**)in, len);
      size_t n;

      req->h2 = wsi_http2(wsi);

      n = headers_write(&req->headers, wsi, &buf.write, buf.end);

#ifdef DEBUG_OUTPUT
      lwsl_user("DEBUG %-22s APPEND_HANDSHAKE_HEADER %zu %zd '%.*s'", __func__, n, buffer_HEAD(&buf), (int)n, buf.read);
#endif

      *(uint8_t**)in += n;

      if(method_number(client->connect_info.method) == METHOD_POST && !lws_http_is_redirected_to_get(wsi)) {
        lws_client_http_body_pending(wsi, 1);
        lws_callback_on_writable(wsi);
      }

      break;
    }

    case LWS_CALLBACK_WSI_DESTROY: {
      if(client->wsi == wsi)
        if(js_async_pending(&client->promise))
          js_async_resolve(ctx, &client->promise, JS_UNDEFINED);

      return -1;
    }

    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {
      lwsl_user("%-26s" FGC(171, "%-34s") "wsi#%d status=%d\n", "CLIENT-HTTP", lws_callback_name(reason) + 13, opaque ? (int)opaque->serial : -1, opaque->resp ? opaque->resp->status : -1);

      return http_client_established(client, wsi, ctx);
    }

    case LWS_CALLBACK_CLOSED_CLIENT_HTTP: {
      if(client->iter)
        asynciterator_stop(client->iter, JS_UNDEFINED, ctx);

      if(opaque->resp) {
        /*if(opaque->resp->body)
          generator_finish(opaque->resp->body);*/

        /*if(opaque->resp->body)
          generator_stop(opaque->resp->body, JS_UNDEFINED);*/
      }

      if(callback_valid(&client->on.close)) {
        JSValueConst argv[] = {
            opaque->ws ? JS_DupValue(client->on.close.ctx, session->ws_obj) : JS_NewInt32(ctx, opaque->fd),
            JS_NewInt32(client->on.close.ctx, 0),
        };

        minnet_client_exception(client, callback_emit(&client->on.close, countof(argv), argv));

        JS_FreeValue(client->on.close.ctx, argv[0]);
        JS_FreeValue(client->on.close.ctx, argv[1]);
      }

      if(opaque->ws)
        opaque->ws->lwsi = NULL;

      opaque->status = CLOSED;

      return -1;
    }

    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE: {
      return http_client_writable(client, wsi, ctx);
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: {
      int ret;
      static uint8_t buffer[1024 + LWS_PRE];
      ByteBuffer buf = BUFFER(buffer);
      int len = buffer_AVAIL(&buf);

      if((ret = lws_http_client_read(wsi, (char**)&buf.write, &len)))
        return -1;

      return 0;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: {
      MinnetResponse* resp = opaque->resp;

      LOGCB("CLIENT-HTTP(2)", "len=%zu in='%.*s'", len, len > 30 ? 30 : (int)len, (char*)in);

      /*if(client->iter) {
        BOOL binary = lws_frame_is_binary(client->wsi);
        ByteBlock blk = block_copy(in, len);
        JSValue chunk = binary ? block_toarraybuffer(&blk, ctx) : block_tostring(&blk, ctx);
        BOOL ok = 0;

        ok = asynciterator_yield(client->iter, chunk, ctx);

        JS_FreeValue(ctx, chunk);

        if(ok)
          return 0;
      }*/

      if(!JS_IsObject(session->resp_obj))
        session->resp_obj = minnet_response_wrap(ctx, opaque->resp);

#ifdef DEBUG_OUTPUT
      lwsl_user("DEBUG %-22s LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ len=%zu in='%.*s'", __func__, len, /*len > 30 ? 30 :*/ (int)len, (char*)in);
#endif

      if(!resp->body)
        resp->body = generator_new(ctx);

      generator_write(resp->body, in, len, JS_UNDEFINED);

      return 0;
    }

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: {
      LOGCB("CLIENT-HTTP(2)", "resp->body=%p resp->body->q=%p", opaque->resp ? opaque->resp->body : 0, opaque->resp && opaque->resp->body ? opaque->resp->body->q : 0);

      return http_client_completed(client, wsi, opaque);
    }

    case LWS_CALLBACK_CLIENT_HTTP_REDIRECT: {
      // lws_wsi_close(wsi, LWS_TO_KILL_SYNC);

      client->lwsret = 1;

      return 0;
    }

    case LWS_CALLBACK_PROTOCOL_DESTROY: {
      break;
    }

    default: {
      minnet_lws_unhandled(__func__, reason);
      break;
    }
  }

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

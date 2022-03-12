#include "minnet-client.h"
#include "minnet-websocket.h"
#include "minnet-response.h"
#include "jsutils.h"

int
http_client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  //  MinnetHttpMethod method = -1;
  if(reason == LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS)
    return 0;
  //
  MinnetClient* client = /*session && session->client ? session->client :*/ lws_client(wsi);
  MinnetSession* session = &client->session;
  JSContext* ctx = client->context.js;
  struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);

  if(!opaque->sess && session)
    opaque->sess = session;

  if(lws_is_poll_callback(reason))
    return fd_callback(wsi, reason, &client->cb.fd, in);

  lwsl_user("client-http " FG("%d") "%-38s" NC " is_ssl=%i len=%zu in='%.*s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, lws_is_ssl(wsi), len, (int)MIN(len, 32), (char*)in);

  switch(reason) {
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
    case LWS_CALLBACK_WSI_CREATE:
    case LWS_CALLBACK_CONNECTING:
    case LWS_CALLBACK_PROTOCOL_INIT: {
      return 0;
    }
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      if(js_promise_pending(&client->promise)) {
        JSValue err = js_error_new(ctx, "%s", in);
        js_promise_reject(ctx, &client->promise, err);
        JS_FreeValue(ctx, err);
      }
      return -1;

      lwsl_user("http-error #1 " FGC(171, "%-38s") "  in: %s\n", lws_callback_name(reason) + 13, in ? (char*)in : "(null)");
      break;
    }

    case LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL: {
      session->req_obj = JS_NULL;
      session->resp_obj = JS_NULL;
      break;
    }
    case LWS_CALLBACK_CLIENT_HTTP_DROP_PROTOCOL: {
      break;
    }
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
      MinnetBuffer buf = BUFFER_N(*(uint8_t**)in, len);

      // buf.start = scan_backwards(buf.start, '\0');
      //
      if(JS_IsObject(client->headers)) {

        // client->request->headers.start = buf.start;

        if(headers_add(&buf, wsi, client->headers, ctx))
          return -1;

        // client->request->headers.end = buf.end;

        *(uint8_t**)in = buf.write;
        len = buf.end - buf.write;
      }

      /* if(!lws_http_is_redirected_to_get(wsi)) {
         lws_client_http_body_pending(wsi, 1);
         lws_callback_on_writable(wsi);
       }*/

      break;
    }

    case LWS_CALLBACK_WSI_DESTROY: {
      if(js_promise_pending(&client->promise))
        js_promise_resolve(ctx, &client->promise, JS_UNDEFINED);
      return -1;
      break;
    }
    case LWS_CALLBACK_CLOSED_CLIENT_HTTP: {
      if(opaque->status < CLOSED) {
        opaque->status = CLOSED;
        if((client->cb.close.ctx = ctx)) {
          JSValueConst cb_argv[] = {JS_DupValue(ctx, session->ws_obj), JS_NewInt32(ctx, opaque->error)};
          client_exception(client, minnet_emit(&client->cb.close, countof(cb_argv), cb_argv));
          JS_FreeValue(ctx, cb_argv[0]);
          JS_FreeValue(ctx, cb_argv[1]);
        }
      }
      break;
    }
    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {
      int status;
      status = lws_http_client_http_response(wsi);

      lwsl_user("http-established #1 " FGC(171, "%-38s") "  server response: %d\n", lws_callback_name(reason) + 13, status);
      // session->req_obj = minnet_request_wrap(ctx, client->request);

      session->resp_obj = minnet_response_new(ctx, url_dup(client->request->url, ctx), status, TRUE, "text/html");

      client->response = minnet_response_data(session->resp_obj);

      if(method_number(client->connect_info.method) == METHOD_POST) {
        lws_client_http_body_pending(wsi, 1);
        lws_callback_on_writable(wsi);
      }

      break;
    }
    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
    case LWS_CALLBACK_HTTP_WRITEABLE: {
      JSValue value, next = JS_NULL;
      BOOL done = FALSE;
      int n;
      ssize_t size, r, i;
      MinnetRequest* req = client->request;
      MinnetBuffer buf;

      buffer_alloc(&buf, 1024, ctx);

      if(lws_http_is_redirected_to_get(wsi))
        break;

      while(!done) {
        value = js_iterator_next(ctx, client->body, &next, &done, 0, 0);

        // printf("\x1b[2K\rdone %s\n", done ? "TRUE" : "FALSE");
        // printf("\x1b[2K\ryielded %s\n", JS_ToCString(ctx, value));

        if(!js_is_nullish(value)) {
          JSBuffer b = js_buffer_from(ctx, value);
          printf("\x1b[2K\ryielded %p %zu\n", b.data, b.size);

          // if(lws_client_http_multipart(wsi, "text", NULL, NULL, &buf.write, buf.end)) return -1;

          // if(lws_client_http_multipart(wsi, "text", NULL, NULL, &buf.write, buf.end)) return -1;

          buffer_append(&buf, b.data, b.size, ctx);
          printf("\x1b[2K\rbuffered %zu/%zu bytes\n", buffer_BYTES(&buf), buffer_HEAD(&buf));
          js_buffer_free(&b, ctx);
        }
      }

      n = done ? LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP;
      size = buf.write - buf.start;

      if((r = lws_write(wsi, buf.start, size, (enum lws_write_protocol)n)) != size)
        return 1;

      // req->body.read += r;

      printf("\x1b[2K\rwrote %zd%s\n", r, n == LWS_WRITE_HTTP_FINAL ? " (final)" : "");

      if(n != LWS_WRITE_HTTP_FINAL)
        lws_callback_on_writable(wsi);

      return 0;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: {
      int ret;
      static uint8_t buffer[1024 + LWS_PRE];

      MinnetBuffer buf = BUFFER(buffer);
      int len = buffer_AVAIL(&buf);

      lwsl_user("http #1  " FGC(171, "%-38s") " fd=%d buf=%p write=%zu len=%d\n", lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), block_BEGIN(&buf), buffer_HEAD(&buf), len);

      ret = lws_http_client_read(wsi, (char**)&buf.write, &len);
      if(ret)
        return -1;

      return 0;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: {
      lwsl_user("http-read #1  " FGC(171, "%-38s") " " FGC(226, "fd=%d") " " FGC(87, "len=%zu") " " FGC(125, "in='%.*s'") "\n",
                lws_callback_name(reason) + 13,
                lws_get_socket_fd(wsi),
                len,
                (int)MIN(len, 32),
                (char*)in);
      MinnetResponse* resp = minnet_response_data2(ctx, session->resp_obj);
      buffer_append(&resp->body, in, len, ctx);
      return 0;
    }

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: {
      MinnetResponse* resp = client->response;
      headers_get(ctx, &resp->headers, wsi);

      if((client->cb.http.ctx = ctx)) {
        int32_t result = -1;
        JSValue ret;
        ret = minnet_emit(&client->cb.http, 2, &session->req_obj);

        /*MinnetWebsocket* ws = minnet_ws_data2(ctx, session->ws_obj);
        JSValue msg = ws->binary ? JS_NewArrayBufferCopy(ctx, in, len) : JS_NewStringLen(ctx, in, len);*/
        if(!client_exception(client, ret)) {
          if(JS_IsNumber(ret))
            JS_ToInt32(ctx, &result, ret);
        }

        return result;
      }
      break;
    }

    default: {
      minnet_lws_unhandled(__func__, reason);
      break;
    }
  }

  /* if(reason < LWS_CALLBACK_ADD_POLL_FD || reason > LWS_CALLBACK_UNLOCK_POLL)
     lwsl_notice("client-http  %-38s fd=%i, in='%.*s'\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);
 */
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

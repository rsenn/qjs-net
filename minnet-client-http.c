#include "minnet-client.h"
#include "minnet-websocket.h"
#include "minnet-response.h"
#include "jsutils.h"

int
http_client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  uint8_t buffer[1024 + LWS_PRE];
  MinnetHttpMethod method = -1;
  MinnetSession* sess = user;
  MinnetClient* client = lws_context_user(lws_get_context(wsi));
  JSContext* ctx = client->ctx;
  struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);
  int n;

  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL:
    case LWS_CALLBACK_ADD_POLL_FD:
    case LWS_CALLBACK_DEL_POLL_FD:
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      return fd_callback(wsi, reason, &client->cb_fd, in);
    }
  }

  lwsl_user("client-http " FG("%d") "%-25s" NC " is_ssl=%i len=%zu in='%.*s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, lws_is_ssl(wsi), len, (int)MIN(len, 32), (char*)in);

  switch(reason) {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      lwsl_user("http-error #1 " FGC(171, "%-25s") "  in: %s\n", lws_callback_name(reason) + 13, in ? (char*)in : "(null)");
      break;
    }

    case LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL: {
      break;
    }
    case LWS_CALLBACK_CLIENT_HTTP_DROP_PROTOCOL: {
      break;
    }
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
      MinnetBuffer buf = BUFFER_N(*(uint8_t**)in, len);

      // buf.start = scan_backwards(buf.start, '\0');

      if(headers_from(&buf, wsi, client->headers, ctx))
        return -1;

      *(uint8_t**)in = buf.write;
      len = buf.end - buf.write;

      /* if(!lws_http_is_redirected_to_get(wsi)) {
         lws_client_http_body_pending(wsi, 1);
         lws_callback_on_writable(wsi);
       }*/

      break;
    }

    case LWS_CALLBACK_CLOSED_CLIENT_HTTP: {
      if(opaque->status < CLOSED) {
        opaque->status = CLOSED;
        if((client->cb_close.ctx = ctx)) {
          JSValueConst cb_argv[] = {JS_DupValue(ctx, sess->ws_obj), JS_NewInt32(ctx, opaque->error)};
          minnet_emit(&client->cb_close, countof(cb_argv), cb_argv);
          JS_FreeValue(ctx, cb_argv[0]);
          JS_FreeValue(ctx, cb_argv[1]);
        }
      }
      break;
    }
    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {
      int status;
      status = lws_http_client_http_response(wsi);
      lwsl_user("http-established #1 " FGC(171, "%-25s") "  server response: %d\n", lws_callback_name(reason) + 13, status);
      sess->resp_obj = minnet_response_new(ctx, client->request->url, status, TRUE, "text/html");
      sess->req_obj = minnet_request_wrap(ctx, client->request);

      if(method_number(client->info.method) == METHOD_POST) {
        lws_client_http_body_pending(wsi, 1);
        lws_callback_on_writable(wsi);
      }

      break;
    }
    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
    case LWS_CALLBACK_HTTP_WRITEABLE: {
      JSValue value, next = JS_NULL;
      BOOL done = FALSE;
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
          printf("\x1b[2K\rbuffered %zu/%zu bytes\n", buffer_REMAIN(&buf), buffer_WRITE(&buf));
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
      MinnetBuffer buf = BUFFER(buffer);
      lwsl_user("http #1  " FGC(171, "%-25s") " fd=%d buf=%p write=%zu len=%d\n", lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), buffer_BEGIN(&buf), buffer_WRITE(&buf), len);
      ret = lws_http_client_read(wsi, &buf, &len);
      if(ret)
        return -1;

      return 0;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: {
      lwsl_user("http-read #1  " FGC(171, "%-25s") " " FGC(226, "fd=%d") " " FGC(87, "len=%zu") " " FGC(125, "in='%.*s'") "\n",
                lws_callback_name(reason) + 13,
                lws_get_socket_fd(wsi),
                len,
                (int)MIN(len, 32),
                (char*)in);
      MinnetResponse* resp = minnet_response_data2(ctx, sess->resp_obj);
      buffer_append(&resp->body, in, len, ctx);
      return 0;
    }

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: {
      /*MinnetResponse* resp = minnet_response_data2(ctx, sess->resp_obj);
      sess->done = TRUE;
      in = buffer_BEGIN(&resp->body);
      len = buffer_WRITE(&resp->body);*/

      if((client->cb_http.ctx = ctx)) {
        /*MinnetWebsocket* ws = minnet_ws_data2(ctx, sess->ws_obj);
        JSValue msg = ws->binary ? JS_NewArrayBufferCopy(ctx, in, len) : JS_NewStringLen(ctx, in, len);*/
        minnet_emit(&client->cb_http, 2, &sess->req_obj);
      }
      break;
    }

    default: {
      // minnet_lws_unhandled(0, reason);
      break;
    }
  }

  /* if(reason < LWS_CALLBACK_ADD_POLL_FD || reason > LWS_CALLBACK_UNLOCK_POLL)
     lwsl_notice("client-http  %-25s fd=%i, in='%.*s'\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);
 */
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

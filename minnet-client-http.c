#include "minnet-client.h"
#include "minnet-websocket.h"
#include "minnet-response.h"
#include "jsutils.h"

int
http_client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE];
  MinnetHttpMethod method = -1;
  MinnetSession* cli = user;
  MinnetClient* client = lws_context_user(lws_get_context(wsi));
  JSContext* ctx = client->ctx;
  int n;

  lwsl_user("client-http " FG("%d") "%-25s" NC " is_ssl=%i len=%zu in='%.*s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, lws_is_ssl(wsi), len, (int)MIN(len, 32), (char*)in);

  switch(reason) {

    case LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL: {
      // struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);

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
      if((client->cb_close.ctx = ctx)) {
        struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);
        int err = opaque ? opaque->error : 0;
        JSValueConst cb_argv[] = {
            minnet_ws_object(ctx, wsi),
            JS_NULL, //  close_status(ctx, in, len),
            JS_NULL, // close_reason(ctx, in, len),
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
    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {
      struct lws_context* lwsctx = lws_get_context(wsi);
      MinnetClient* client = lws_context_user(lwsctx);

      if(cli && !cli->connected) {
        const char* method = client->info.method;

        if(!minnet_ws_data(cli->ws_obj))
          cli->ws_obj = minnet_ws_object(ctx, wsi);

        cli->connected = TRUE;
        cli->req_obj = minnet_request_wrap(ctx, client->request);

        // lwsl_user("client   " FGC(171, "%-25s") " fd=%i, in=%.*s\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);
        minnet_emit(&client->cb_connect, 2, &cli->ws_obj);

        cli->resp_obj = minnet_response_new(ctx, "/", /* method == METHOD_POST ? 201 :*/ 200, TRUE, "text/html");

        if(method_number(method) == METHOD_POST) {
          lws_client_http_body_pending(wsi, 1);
          lws_callback_on_writable(wsi);
        }
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
      char buffer[1024 + LWS_PRE];
      char* buf = buffer + LWS_PRE;
      int ret, len = sizeof(buffer) - LWS_PRE;
      // lwsl_user("http#1  " FGC(171, "%-25s") " fd=%d buf=%p len=%d\n", lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), buf, len);
      ret = lws_http_client_read(wsi, &buf, &len);
      lwsl_user("http-read " FGC(171, "%-25s") " fd=%d ret=%d buf=%p len=%d\n", lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), ret, buf, len);
      if(ret)
        return -1;

      if(!cli->responded) {
        cli->responded = TRUE;
        // if(JS_IsUndefined(cli->resp_obj)) cli->resp_obj = minnet_response_new(ctx, "/", /* method == METHOD_POST ? 201 :*/ 200, TRUE, "text/html");
      }
      return 0;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: {
      // lwsl_user("http#read  " FGC(171, "%-25s") " " FGC(226, "fd=%d") " " FGC(87, "len=%zu") " " FGC(125, "in='%.*s'") "\n", lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), len,
      // (int)MIN(len, 32), (char*)in);
      MinnetResponse* resp = minnet_response_data2(ctx, cli->resp_obj);
      buffer_append(&resp->body, in, len, ctx);
      return 0;
    }

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: {
      MinnetResponse* resp = minnet_response_data2(ctx, cli->resp_obj);
      cli->done = TRUE;
      in = buffer_BEGIN(&resp->body);
      len = buffer_WRITE(&resp->body);

      if((client->cb_message.ctx = ctx)) {
        MinnetWebsocket* ws = minnet_ws_data2(ctx, cli->ws_obj);
        JSValue msg = ws->binary ? JS_NewArrayBufferCopy(ctx, in, len) : JS_NewStringLen(ctx, in, len);
        JSValue cb_argv[3] = {cli->ws_obj, cli->req_obj, msg};
        minnet_emit(&client->cb_message, 3, cb_argv);
      }
      return 0;
    }
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL:
    case LWS_CALLBACK_ADD_POLL_FD:
    case LWS_CALLBACK_DEL_POLL_FD:
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      return fd_callback(wsi, reason, &client->cb_fd, in);
    }

    default: {
      // minnet_lws_unhandled(0, reason);
      break;
    }
  }

  if(reason < LWS_CALLBACK_ADD_POLL_FD || reason > LWS_CALLBACK_UNLOCK_POLL)
    lwsl_notice("client-http  %-25s fd=%i, in='%.*s'\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

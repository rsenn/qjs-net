#include "minnet-client.h"
#include "minnet-client-http.h"
#include "minnet-websocket.h"
#include "minnet-response.h"
#include "minnet.h"
#include "headers.h"
#include "js-utils.h"
#include <libwebsockets.h>

typedef struct {
  int ref_count;
  JSContext* ctx;
  struct session_data* session;
  MinnetResponse* resp;
  struct lws* wsi;
} HTTPAsyncResolveClosure;

int
http_client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {

  if(reason == LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS)
    return 0;

  MinnetClient* client = lws_client(wsi);
  struct session_data* session = &client->session;
  JSContext* ctx = client ? client->context.js : 0;
  struct wsi_opaque_user_data* opaque;

  if(lws_reason_poll(reason))
    return wsi_handle_poll(wsi, reason, &client->on.fd, in);

  if((opaque = lws_opaque(wsi, ctx))) {
    if(!opaque->sess && session)
      opaque->sess = session;
  }

  // lwsl_user("client-http " FG("%d") "%-38s" NC " is_ssl=%i len=%zu in='%.*s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, wsi_tls(wsi), len, (int)MIN(len, 32), (char*)in);
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

      /*      MinnetRequest* req = client->request;
            MinnetResponse* resp;

            if(req) {
              session->req_obj = minnet_request_wrap(ctx, client->request);
            }*/

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
      if(js_async_pending(&client->promise)) {
        JSValue err = js_error_new(ctx, "%s", (char*)in);
        js_async_reject(ctx, &client->promise, err);
        JS_FreeValue(ctx, err);
      }

      if(client->on.close.ctx) {
        JSValueConst argv[] = {
            opaque->ws ? JS_DupValue(ctx, session->ws_obj) : JS_NewInt32(ctx, opaque->fd),
            js_error_new(ctx, "%s", (char*)in),
        };

        client_exception(client, callback_emit(&client->on.close, countof(argv), argv));

        JS_FreeValue(ctx, argv[0]);
        JS_FreeValue(ctx, argv[1]);
      }

      return -1;
    }

    case LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL: {
      if(opaque) {
        if(opaque->fd != -1) {
          if(client->on.close.ctx) {
            JSValueConst argv[] = {
                opaque->ws ? JS_DupValue(client->on.close.ctx, session->ws_obj) : JS_NewInt32(client->on.close.ctx, opaque->fd),
                JS_NewInt32(client->on.close.ctx, 0),
            };
            client_exception(client, callback_emit(&client->on.close, countof(argv), argv));
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

      req->h2 = wsi_http2(wsi);

      size_t n = headers_write(&req->headers, wsi, &buf.write, buf.end);

#ifdef DEBUT_OUTPUT
      printf("APPEND_HANDSHAKE_HEADER %zu %zd '%.*s'\n", n, buffer_HEAD(&buf), (int)n, buf.read);
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
      break;
    }

    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {
      int ret = 0;
      MinnetResponse* resp;
      char* type;

      if(strcmp(lws_get_protocol(wsi)->name, "ws"))
        opaque->status = OPEN;

      // client->req->h2 = wsi_http2(wsi);

      if(!(resp = opaque->resp)) {
        resp = opaque->resp = response_new(ctx);
        // resp->body = generator_dup(client_generator(client, ctx));
        resp->status = lws_http_client_http_response(wsi);

        headers_tobuffer(ctx, &opaque->resp->headers, wsi);

        session->resp_obj = minnet_response_wrap(ctx, opaque->resp);
      }

      if((type = response_type(resp, ctx))) {
        if(!strncmp(type, "text/", 5))
          resp->body->block_fn = &block_tostring;
        js_free(ctx, type);
      }

      url_copy(&resp->url, client->request->url, ctx);

      // opaque->resp->headers = headers_gettoken(ctx, wsi, WSI_TOKEN_HTTP_CONTENT_TYPE);

      if(!opaque->ws)
        opaque->ws = ws_new(wsi, ctx);

      session->ws_obj = minnet_ws_wrap(ctx, opaque->ws);

      lwsl_user("%-26s" FGC(171, "%-34s") "wsi#%d status=%d\n", "CLIENT-HTTP", lws_callback_name(reason) + 13, opaque ? (int)opaque->serial : -1, resp->status);

      {
        size_t i, hdrlen = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP);
        char buf[(((hdrlen + 1) + 7) >> 3) << 3];
        lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_HTTP);
        buf[hdrlen] = '\0';
        if(buf[(i = byte_chr(buf, hdrlen, ' '))])
          i += 1;
        client->response->status_text = js_strdup(ctx, &buf[i]);
      }

      if(js_async_pending(&client->promise)) {
        JSValue cli = minnet_client_wrap(ctx, client_dup(client));

        js_async_resolve(ctx, &client->promise, cli);

        JS_FreeValue(ctx, cli);
      }

      if(client->on.http.ctx) {
        JSValue retval = client_exception(client, callback_emit_this(&client->on.http, session->ws_obj, 2, &session->req_obj));
        if(!js_is_nullish(retval)) {
          BOOL terminate = JS_ToBool(ctx, retval);

          if(terminate) {
            client->lwsret = 1;
          }
        }
        JS_FreeValue(client->on.http.ctx, retval);
      }

      if(resp->status >= 400) {
        // generator_continuous(resp->body, JS_NULL);
        // lws_set_timeout(wsi, 1, LWS_TO_KILL_ASYNC);
        lws_wsi_close(wsi, LWS_TO_KILL_SYNC);
      }

      if(method_number(client->connect_info.method) == METHOD_POST) {
        lws_client_http_body_pending(wsi, 1);
        lws_callback_on_writable(wsi);
      }

      return ret;
    }

    case LWS_CALLBACK_CLOSED_CLIENT_HTTP: {

      if(client->iter)
        asynciterator_stop(client->iter, JS_UNDEFINED, ctx);

      if(opaque->resp) {
        /*if(opaque->resp->body)
          generator_finish(opaque->resp->body);*/

        /*      if(opaque->resp->body)
                generator_stop(opaque->resp->body, JS_UNDEFINED);*/
      }

      if(client->on.close.ctx) {
        JSValueConst argv[] = {
            opaque->ws ? JS_DupValue(client->on.close.ctx, session->ws_obj) : JS_NewInt32(ctx, opaque->fd),
            JS_NewInt32(client->on.close.ctx, 0),
        };
        client_exception(client, callback_emit(&client->on.close, countof(argv), argv));
        JS_FreeValue(client->on.close.ctx, argv[0]);
        JS_FreeValue(client->on.close.ctx, argv[1]);
      }

      if(opaque->ws) {
        opaque->ws->lwsi = NULL;
      }

      opaque->status = CLOSED;

      // lws_cancel_service(lws_get_context(wsi)); /* abort poll wait */
      return -1;
    }

    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE: {
      if(client->on.writeable.ctx) {
        JSValue ret;
        opaque->writable = TRUE;
        ret = client_exception(client, callback_emit(&client->on.writeable, 1, &client->session.ws_obj));

        if(JS_IsBool(ret)) {
          if(JS_ToBool(ctx, ret) == FALSE) {
            client->on.writeable = CALLBACK_INIT(0, JS_NULL, JS_NULL);
          }
        }
        opaque->writable = FALSE;

        if(client->on.writeable.ctx)
          lws_callback_on_writable(wsi);
        return 0;
      }

      if(method_number(client->connect_info.method) == METHOD_POST) {
        BOOL done = FALSE;
        JSValue value;
        int n;
        ssize_t size, r;
        // MinnetRequest* req = client->request;
        ByteBuffer buf;
        buffer_alloc(&buf, 1024);

        if(lws_http_is_redirected_to_get(wsi))
          break;

        if(JS_IsObject(client->body)) {
          while(!done) {
            value = js_iterator_next(ctx, client->body, &client->next, &done, 0, 0);

#ifdef DEBUT_OUTPUT
            printf("js_iterator_next() = %s %i done=%i\n", JS_ToCString(ctx, value), JS_VALUE_GET_TAG(value), done);
#endif

            if(JS_IsException(value)) {
              JSValue exception = JS_GetException(ctx);
              js_error_print(ctx, exception);
              JS_Throw(ctx, exception);
            } else if(!js_is_nullish(value)) {
              JSBuffer input = js_buffer_new(ctx, value);
              // js_std_dump_error(ctx);

#ifdef DEBUT_OUTPUT
              printf("\x1b[2K\ryielded %p %zu\n", input.data, input.size);
#endif

              buffer_append(&buf, input.data, input.size);
#ifdef DEBUT_OUTPUT
              printf("\x1b[2K\rbuffered %zu/%zu bytes\n", buffer_REMAIN(&buf), buffer_HEAD(&buf));
#endif

              js_buffer_free(&input, JS_GetRuntime(ctx));
            }

            break;
          }
        } else if(!js_is_nullish(client->body)) {
          buffer_fromvalue(&buf, client->body, ctx);
          done = TRUE;
        }

        n = done ? LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP;
        size = buf.write - buf.start;
        if((r = lws_write(wsi, buf.start, size, (enum lws_write_protocol)n)) != size)
          return 1;
#ifdef DEBUT_OUTPUT
        printf("\x1b[2K\rwrote %zd%s\n", r, n == LWS_WRITE_HTTP_FINAL ? " (final)" : "");
#endif

        if(n != LWS_WRITE_HTTP_FINAL)
          lws_callback_on_writable(wsi);
      }
      return 0;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: {
      int ret;
      static uint8_t buffer[1024 + LWS_PRE];
      ByteBuffer buf = BUFFER(buffer);
      int len = buffer_AVAIL(&buf);
      // lwsl_user("http #1  " FGC(171, "%-38s") " fd=%d buf=%p write=%zu len=%d\n", lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), block_BEGIN(&buf), buffer_HEAD(&buf), len);
      ret = lws_http_client_read(wsi, (char**)&buf.write, &len);
      if(ret)
        return -1;
      return 0;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: {

      /*   if(client->iter) {
           BOOL binary = lws_frame_is_binary(client->wsi);
           ByteBlock blk = block_copy(in, len);
           JSValue chunk = binary ? block_toarraybuffer(&blk, ctx) : block_tostring(&blk, ctx);
           BOOL ok = 0;

           ok = asynciterator_yield(client->iter, chunk, ctx);

           JS_FreeValue(ctx, chunk);
           if(ok)
             return 0;
         }*/

      MinnetResponse* resp = opaque->resp;

      LOGCB("CLIENT-HTTP(2)", "len=%zu in='%.*s'", len, len > 30 ? 30 : (int)len, (char*)in);

      if(!JS_IsObject(session->resp_obj))
        session->resp_obj = minnet_response_wrap(ctx, opaque->resp);

#ifdef DEBUT_OUTPUT
      printf("LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ len=%zu in='%.*s'", len, /*len > 30 ? 30 :*/ (int)len, (char*)in);
#endif

      generator_write(resp->body, in, len, JS_UNDEFINED);

      return 0;
    }

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: {
      MinnetResponse* resp = opaque->resp;
      /*  Generator* gen = resp->body;*/

      LOGCB("CLIENT-HTTP(2)", "resp->body=%p resp->body->q=%p", resp->body, resp->body->q);

      //
      generator_finish(resp->body);

      if(client->on.http.ctx) {
        /*        MinnetRequest* req;
                MinnetResponse* resp = minnet_response_data2(client->on.http.ctx, session->resp_obj);
                int32_t result = -1;
                JSValue ret;
        */

        // url_copy(&resp->url, client->request->url, client->on.http.ctx);

        // resp->type = headers_get(&resp->headers, "content-type", client->on.http.ctx);

        /*    ret = client_exception(client, callback_emit_this(&client->on.http, session->ws_obj, 2, &session->req_obj));

            if(JS_IsNumber(ret)) {
              JS_ToInt32(client->on.http.ctx, &result, ret);

              //printf("onRequest() returned: %" PRId32 "\n", result);
              client->wsi = wsi;

            } else if((req = minnet_request_data(ret))) {
              url_info(req->url, &client->connect_info);
              client->connect_info.pwsi = &client->wsi;
              client->connect_info.context = client->context.lws;

              if(client->request) {
                request_free(client->request, client->on.http.ctx);
                client->request = 0;
              }

              if(client->response) {
                response_free(client->response, client->on.http.ctx);
                client->response = 0;
              }
              if(opaque->resp) {
                response_free(opaque->resp, client->on.http.ctx);
                opaque->resp = 0;
              }

              client->request = req;
              client->response = response_new(client->on.http.ctx);


              lws_client_connect_via_info(&client->connect_info);

              result = 0;
            } else if(js_is_promise(ctx, ret)) {
              JSValue promise = client_promise(ctx, session, resp, wsi, ret);

            } else {
              const char* str = JS_ToCString(ctx, ret);
              JS_ThrowInternalError(client->on.http.ctx, "onRequest didn't return a number: %s", str);
              if(str)
                JS_FreeCString(ctx, str);
            }

            if(result != 0) {
              lws_cancel_service(lws_get_context(wsi));
            }

            return result;*/
      }

      return client->lwsret;
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

  /* if(reason < LWS_CALLBACK_ADD_POLL_FD || reason > LWS_CALLBACK_UNLOCK_POLL)
     lwsl_notice("client-http  %-38s fd=%i, in='%.*s'\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);
  */
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

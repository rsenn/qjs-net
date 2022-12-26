#include "minnet-client.h"
#include "minnet-client-http.h"
#include "minnet-websocket.h"
#include "minnet-response.h"
#include "minnet.h"
#include "headers.h"
#include "jsutils.h"
#include <libwebsockets.h>

typedef struct {
  int ref_count;
  JSContext* ctx;
  struct session_data* session;
  MinnetResponse* resp;
  struct lws* wsi;
} HTTPAsyncResolveClosure;

static void
client_resolved_free(void* ptr) {
  HTTPAsyncResolveClosure* closure = ptr;
  if(--closure->ref_count == 0) {
    response_free(closure->resp, closure->ctx);
    js_free(closure->ctx, ptr);
  }
}

static JSValue
client_resolved(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  HTTPAsyncResolveClosure* closure = ptr;

  const char* val = JS_ToCString(ctx, argv[0]);

  printf("value=%s\n", val);
  LOG(__func__, "value=%s", val);
  JS_FreeCString(ctx, val);

  return JS_UNDEFINED;
}

static JSValue
client_promise(JSContext* ctx, struct session_data* session, MinnetResponse* resp, struct lws* wsi, JSValueConst value) {
  HTTPAsyncResolveClosure* p;
  JSValue ret = JS_UNDEFINED;

  if((p = js_malloc(ctx, sizeof(HTTPAsyncResolveClosure)))) {
    *p = (HTTPAsyncResolveClosure){1, ctx, session, response_dup(resp), wsi};
    JSValue fn = JS_NewCClosure(ctx, client_resolved, 1, 0, p, client_resolved_free);
    JSValue tmp = js_promise_then(ctx, value, fn);
    JS_FreeValue(ctx, fn);
    ret = tmp;
  } else {
    ret = JS_ThrowOutOfMemory(ctx);
  }
  return ret;
}

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
    LOGCB("CLIENT-HTTP ", "fd=%d, h2=%i, tls=%i%s%.*s%s", lws_get_socket_fd(wsi), wsi_http2(wsi), wsi_tls(wsi), (in && len) ? ", in='" : "", (int)len, (char*)in, (in && len) ? "'" : "");

  switch(reason) {
    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH: {

      MinnetRequest* req = client->request;
      MinnetResponse* resp;

      if(req) {
        // url_fromwsi(&req->url, wsi, ctx);

        session->req_obj = minnet_request_wrap(ctx, client->request);
      }

      if(!(resp = opaque->resp)) {
        resp = opaque->resp = response_new(ctx);

        resp->generator = generator_new(ctx);
        resp->status = lws_http_client_http_response(wsi);

        headers_tobuffer(ctx, &opaque->resp->headers, wsi);
        session->resp_obj = minnet_response_wrap(ctx, opaque->resp);
      }
      /*    if(js_is_nullish(session->req_obj))
            session->req_obj = minnet_request_wrap(ctx, client->request);
          if(js_is_nullish(session->resp_obj))
            session->resp_obj = minnet_response_wrap(ctx, client->response);
    */
      return 0;
    }
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_WSI_CREATE:
    case LWS_CALLBACK_CONNECTING: {
      break;
    }
    case LWS_CALLBACK_PROTOCOL_INIT: {
      session_zero(session);
      return 0;
    }
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      if(js_promise_pending(&client->promise)) {
        JSValue err = js_error_new(ctx, "%s", (char*)in);
        js_promise_reject(ctx, &client->promise, err);
        JS_FreeValue(ctx, err);
      }
      return -1;
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

      MinnetRequest* req = client->request;
      ByteBuffer buf = BUFFER_N(*(uint8_t**)in, len);

      size_t n = headers_write(&buf.write, buf.end, &req->headers, wsi);

      DEBUG("APPEND_HANDSHAKE_HEADER %zu %zd '%.*s'\n", n, buffer_HEAD(&buf), (int)n, buf.read);
      *(uint8_t**)in += n;

      if(method_number(client->connect_info.method) == METHOD_POST && !lws_http_is_redirected_to_get(wsi)) {
        lws_client_http_body_pending(wsi, 1);
        lws_callback_on_writable(wsi);
      }
      break;
    }

    case LWS_CALLBACK_WSI_DESTROY: {
      if(client->wsi == wsi) {
        if(js_promise_pending(&client->promise))
          js_promise_resolve(ctx, &client->promise, JS_UNDEFINED);
      }
      return -1;
      break;
    }

    case LWS_CALLBACK_CLOSED_CLIENT_HTTP: {
      if(opaque->status < CLOSED) {
        opaque->status = CLOSED;
        if(client->on.close.ctx) {
          JSValueConst cb_argv[] = {
              JS_DupValue(client->on.close.ctx, session->ws_obj),
              JS_NewInt32(client->on.close.ctx, 0),
          };
          client_exception(client, callback_emit(&client->on.close, countof(cb_argv), cb_argv));
          JS_FreeValue(client->on.close.ctx, cb_argv[0]);
          JS_FreeValue(client->on.close.ctx, cb_argv[1]);
        }

        if(opaque->resp->generator)
          generator_close(opaque->resp->generator, JS_UNDEFINED);
      }
      lws_cancel_service(lws_get_context(wsi)); /* abort poll wait */
      return -1;
    }

    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {
      int status;
      MinnetResponse* resp;

      if(!(resp = opaque->resp)) {
        resp = opaque->resp = response_new(ctx);

        resp->generator = generator_new(ctx);
        resp->status = lws_http_client_http_response(wsi);

        headers_tobuffer(ctx, &opaque->resp->headers, wsi);
        session->resp_obj = minnet_response_wrap(ctx, opaque->resp);
      }

      headers_tobuffer(ctx, &opaque->resp->headers, wsi);

      if(!resp->type)
        resp->type = headers_get(&resp->headers, "content-type", ctx);

      if(!strncmp(resp->type, "text/", 5))
        resp->generator->block_fn = &block_tostring;

      url_copy(&resp->url, client->request->url, client->on.http.ctx);

      // opaque->resp->headers = headers_gettoken(ctx, wsi, WSI_TOKEN_HTTP_CONTENT_TYPE);

      if(!opaque->ws)
        opaque->ws = ws_new(wsi, ctx);
      session->ws_obj = minnet_ws_wrap(ctx, opaque->ws);

      opaque->resp->status = status = lws_http_client_http_response(wsi);

      lwsl_user("http-established #1 " FGC(171, "%-38s") "  server response: %d\n", lws_callback_name(reason) + 13, status);
      {
        size_t i, hdrlen = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP);
        char buf[(((hdrlen + 1) + 7) >> 3) << 3];
        lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_HTTP);
        buf[hdrlen] = '\0';
        if(buf[(i = byte_chr(buf, hdrlen, ' '))])
          i += 1;
        client->response->status_text = js_strdup(ctx, &buf[i]);
      }

      if(method_number(client->connect_info.method) == METHOD_POST) {
        lws_client_http_body_pending(wsi, 1);
        lws_callback_on_writable(wsi);
      }
      return 0;
    }
    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
      /*  case LWS_CALLBACK_HTTP_WRITEABLE: */ {
        if(method_number(client->connect_info.method) == METHOD_POST) {
          JSValue value;
          int n;
          ssize_t size, r;
          // MinnetRequest* req = client->request;
          ByteBuffer buf;
          buffer_alloc(&buf, 1024);

          if(lws_http_is_redirected_to_get(wsi))
            break;
          if(JS_IsObject(client->body)) {
            while(!client->done) {
              value = js_iterator_next(ctx, client->body, &client->next, &client->done, 0, 0);

              DEBUG("js_iterator_next() = %s %i done=%i\n", JS_ToCString(ctx, value), JS_VALUE_GET_TAG(value), client->done);

              if(JS_IsException(value)) {
                JSValue exception = JS_GetException(ctx);
                js_error_print(ctx, exception);
                JS_Throw(ctx, exception);
              } else if(!js_is_nullish(value)) {
                JSBuffer input = js_buffer_new(ctx, value);
                // js_std_dump_error(ctx);

                DEBUG("\x1b[2K\ryielded %p %zu\n", input.data, input.size);
                buffer_append(&buf, input.data, input.size);
                DEBUG("\x1b[2K\rbuffered %zu/%zu bytes\n", buffer_REMAIN(&buf), buffer_HEAD(&buf));
                js_buffer_free(&input, ctx);
              }

              break;
            }
          }
          n = client->done ? LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP;
          size = buf.write - buf.start;
          if((r = lws_write(wsi, buf.start, size, (enum lws_write_protocol)n)) != size)
            return 1;
          DEBUG("\x1b[2K\rwrote %zd%s\n", r, n == LWS_WRITE_HTTP_FINAL ? " (final)" : "");
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
      MinnetResponse* resp = opaque->resp;

      LOGCB("CLIENT-HTTP(2)", "len=%zu in='%.*s'", len, len > 30 ? 30 : (int)len, (char*)in);

      if(!JS_IsObject(session->resp_obj))
        session->resp_obj = minnet_response_wrap(ctx, opaque->resp);

      generator_write(resp->generator, in, len, JS_UNDEFINED);

      return 0;
    }

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: {
      if(client->on.http.ctx) {
        MinnetRequest* req;
        MinnetResponse* resp = minnet_response_data2(client->on.http.ctx, session->resp_obj);
        int32_t result = -1;
        JSValue ret;

        // url_copy(&resp->url, client->request->url, client->on.http.ctx);

        // resp->type = headers_get(&resp->headers, "content-type", client->on.http.ctx);

        ret = client_exception(client, callback_emit_this(&client->on.http, session->ws_obj, 2, &session->req_obj));

        if(JS_IsNumber(ret)) {
          JS_ToInt32(client->on.http.ctx, &result, ret);

          printf("onHttp() returned: %" PRId32 "\n", result);
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

          //   opaque->req = request_dup(req);

          lws_client_connect_via_info(&client->connect_info);

          result = 0;
        } else if(js_is_promise(ctx, ret)) {
          JSValue promise = client_promise(ctx, session, resp, wsi, ret);

        } else {
          const char* str = JS_ToCString(ctx, ret);
          JS_ThrowInternalError(client->on.http.ctx, "onHttp didn't return a number: %s", str);
          if(str)
            JS_FreeCString(ctx, str);
        }

        if(result != 0) {
          lws_cancel_service(lws_get_context(wsi)); /* abort poll wait */
        }

        return result;
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

  /* if(reason < LWS_CALLBACK_ADD_POLL_FD || reason > LWS_CALLBACK_UNLOCK_POLL)
     lwsl_notice("client-http  %-38s fd=%i, in='%.*s'\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), (int)len, (char*)in);
 */
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

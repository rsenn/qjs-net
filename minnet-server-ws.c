#include "minnet-server.h"
#include "minnet-websocket.h"
#include "minnet-request.h"
#include "minnet-response.h"
#include <assert.h>

int http_server_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

int
ws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetServer* server = lws_context_user(lws_get_context(wsi));
  MinnetSession* session = user;
  JSContext* ctx = server ? server->context.js : 0;
  struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);

  if(lws_is_poll_callback(reason)) {
    assert(server);
    return fd_callback(wsi, reason, &server->cb.fd, in);
  }
  if(ctx) {
    if(!opaque)
      opaque = lws_opaque(wsi, ctx);
  }

  if(opaque->ws && !JS_IsObject(session->ws_obj))
    session->ws_obj = minnet_ws_wrap(ctx, wsi);

  if(session && !session->server)
    session->server = server;

  if(lws_is_http_callback(reason))
    return http_server_callback(wsi, reason, user, in, len);

  switch(reason) {
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION: {
      opaque->ws = ws_new(wsi, ctx);
      break;
    }

    case LWS_CALLBACK_PROTOCOL_INIT: {

      break;
    }

    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS:
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: {
      return lws_callback_http_dummy(wsi, reason, user, in, len);
    }

    case LWS_CALLBACK_WSI_CREATE: {
      if(!opaque)
        if(ctx)
          opaque = lws_opaque(wsi, ctx);

      return 0;
    }

    case LWS_CALLBACK_WSI_DESTROY: {
      return 0;
    }

    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: {
      if(!lws_is_ssl(wsi) && !strcmp(in, "h2c"))
        return -1;

      /*return http_server_callback(wsi, reason, user, in, len);*/
      if(!opaque)
        opaque = lws_opaque(wsi, ctx);
      assert(opaque);

      opaque->req = request_new(ctx, /*in, */ url_create(lws_get_uri(wsi, ctx, WSI_TOKEN_GET_URI), ctx), METHOD_GET);

      int num_hdr = headers_get(ctx, &opaque->req->headers, wsi);
      break;
    }

    case LWS_CALLBACK_ESTABLISHED: {
      // struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);
      int status;
      status = lws_http_client_http_response(wsi);

      opaque->status = OPEN;

      if(server->cb.connect.ctx) {

        if(!opaque->req)
          opaque->req = request_new(ctx, /* in,*/ url_create(lws_get_uri(wsi, ctx, WSI_TOKEN_GET_URI), ctx), METHOD_GET);

        assert(opaque->req);

        if(!JS_IsObject(session->req_obj))
          session->req_obj = minnet_request_wrap(ctx, opaque->req);

        session->resp_obj = minnet_response_new(ctx, opaque->req->url, status, 0, TRUE, "text/html");

        lwsl_user("ws   " FG("%d") "%-38s" NC " wsi#%" PRId64 " req=%p\n", 22 + (reason * 2), lws_callback_name(reason) + 13, opaque->serial, opaque->req);
        server_exception(server, minnet_emit_this(&server->cb.connect, session->ws_obj, 2, &session->ws_obj));
      }

      return 0;
    }

    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
    case LWS_CALLBACK_CLOSED: {
      if(opaque->status < CLOSING) {
        if(ctx) {
          JSValue why = JS_UNDEFINED;
          int code = -1;

          if(in) {
            uint8_t* codep = in;
            code = (codep[0] << 8) + codep[1];
            if(len - 2 > 0)
              why = JS_NewStringLen(server->context.js, (char*)in + 2, len - 2);
          }

          opaque->status = CLOSING;

          lwsl_user("ws   " FG("%d") "%-38s" NC " fd=%d, status=%d\n", 22 + (reason * 2), lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), opaque->status);

          JSValue cb_argv[3] = {session->ws_obj, code != -1 ? JS_NewInt32(ctx, code) : JS_UNDEFINED, why};
          server_exception(server, minnet_emit(&server->cb.close, code != -1 ? 3 : 1, cb_argv));
          JS_FreeValue(ctx, cb_argv[1]);
          JS_FreeValue(server->context.js, why);
        }
        /*JS_FreeValue(server->context.js, session->ws_obj);
        session->ws_obj = JS_NULL;*/
      }
      break;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
      fprintf(stderr, "\x1b[1;33mwritable\x1b[0m %s fd=%d\n", lws_callback_name(reason) + 13, lws_get_socket_fd(wsi));

      MinnetBuffer* buf = &session->send_buf;
      fprintf(stderr, "\x1b[1;33mwritable\x1b[0m %s buf=%s\n", lws_callback_name(reason) + 13, buffer_escaped(buf, ctx));

      break;
    }

    case LWS_CALLBACK_RECEIVE: {
      if(ctx) {
        MinnetWebsocket* ws = minnet_ws_data2(ctx, session->ws_obj);
        JSValue msg = opaque->binary ? JS_NewArrayBufferCopy(ctx, in, len) : JS_NewStringLen(ctx, in, len);
        JSValue cb_argv[2] = {JS_DupValue(ctx, session->ws_obj), msg};
        server_exception(server, minnet_emit(&server->cb.message, 2, cb_argv));
        JS_FreeValue(ctx, cb_argv[0]);
        JS_FreeValue(ctx, cb_argv[1]);
      }
      return 0;
    }
    case LWS_CALLBACK_RECEIVE_PONG: {
      if(server->cb.pong.ctx) {
        // ws_obj = minnet_ws_wrap(server->cb.pong.ctx, wsi);
        JSValue msg = JS_NewArrayBufferCopy(server->cb.pong.ctx, in, len);
        JSValue cb_argv[2] = {JS_DupValue(server->cb.pong.ctx, session->ws_obj), msg};
        server_exception(server, minnet_emit(&server->cb.pong, 2, cb_argv));
        JS_FreeValue(server->cb.pong.ctx, cb_argv[0]);
        JS_FreeValue(server->cb.pong.ctx, cb_argv[1]);
      }
      return 0;
    }
    case LWS_CALLBACK_WS_SERVER_DROP_PROTOCOL: {
      return 0;
    }

    default: {
      // printf("ws_callback %s %p %p %zu\n", lws_callback_name(reason), user, in, len);
      minnet_lws_unhandled(__func__, reason);
      break;
    }
  }

  // lwsl_user("ws   " FG("%d") "%-38s" NC " fd=%d url='%s' in='%.*s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), lws_get_uri(wsi, server->context.js,
  // WSI_TOKEN_GET_URI), (int)len, (char*)in);

  if(opaque && opaque->status >= CLOSING)
    return -1;

  return 0; // lws_callback_http_dummy(wsi, reason, user, in, len);
}

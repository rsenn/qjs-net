#include "minnet-server.h"
#include "minnet-websocket.h"
#include "minnet-request.h"

int http_server_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

int
ws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetSession* sess = user;
  JSContext* ctx = minnet_server.ctx;
  struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);

  switch(reason) {
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_PROTOCOL_INIT: {
      return lws_callback_http_dummy(wsi, reason, user, in, len);
    }

    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: {

      if(!lws_is_ssl(wsi) && !strcmp(in, "h2c"))
        return -1;
      /* return 0;
       return http_server_callback(wsi, reason, user, in, len);*/
      /* if(minnet_server.cb_connect.ctx) {*/
      struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);

      opaque->req = request_new(ctx, in, lws_get_uri(wsi, ctx, WSI_TOKEN_GET_URI), METHOD_GET);

      int num_hdr = http_server_headers(ctx, &opaque->req->headers, wsi);
      break;
    }

    case LWS_CALLBACK_ESTABLISHED: {
      struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);

      if(minnet_server.cb_connect.ctx) {

        if(!opaque->req)
          opaque->req = request_new(ctx, in, lws_get_uri(wsi, ctx, WSI_TOKEN_GET_URI), METHOD_GET);

        assert(opaque->req);

        if(!JS_IsObject(sess->req_obj))
          sess->req_obj = minnet_request_wrap(ctx, opaque->req);

        sess->ws_obj = minnet_ws_wrap(ctx, wsi);
        opaque->ws = minnet_ws_data2(ctx, sess->ws_obj);

        lwsl_user("ws   " FG("%d") "%-25s" NC " wsi#%" PRId64 " req=%p url=%s\n", 22 + (reason * 2), lws_callback_name(reason) + 13, opaque->serial, opaque->req, opaque->req->url);
        minnet_emit_this(&minnet_server.cb_connect, sess->ws_obj, 2, &sess->ws_obj);
      }

      return 0;
    }

    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
    case LWS_CALLBACK_CLOSED: {
      if(sess->status < CLOSING) {
        JSValue why = JS_UNDEFINED;
        int code = -1;

        sess->status = CLOSING;

        if(in) {
          uint8_t* codep = in;
          code = (codep[0] << 8) + codep[1];
          if(len - 2 > 0)
            why = JS_NewStringLen(minnet_server.ctx, (char*)in + 2, len - 2);
        }

        lwsl_user("ws   " FG("%d") "%-25s" NC " fd=%d\n", 22 + (reason * 2), lws_callback_name(reason) + 13, lws_get_socket_fd(wsi));

        if(ctx) {
          JSValue cb_argv[3] = {JS_DupValue(ctx, sess->ws_obj), code != -1 ? JS_NewInt32(ctx, code) : JS_UNDEFINED, why};
          minnet_emit(&minnet_server.cb_close, code != -1 ? 3 : 1, cb_argv);
          JS_FreeValue(ctx, cb_argv[0]);
          JS_FreeValue(ctx, cb_argv[1]);
        }
        JS_FreeValue(minnet_server.ctx, why);
        /*JS_FreeValue(minnet_server.ctx, sess->ws_obj);
        sess->ws_obj = JS_NULL;*/
      }
      return 0;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
      /*   printf("ws   %s fd=%d\n", lws_callback_name(reason), lws_get_socket_fd(wsi));
         lws_callback_on_writable(wsi);*/
      return 0;
    }
    case LWS_CALLBACK_RECEIVE: {
      if(ctx) {
        MinnetWebsocket* ws = minnet_ws_data2(ctx, sess->ws_obj);
        JSValue msg = ws->binary ? JS_NewArrayBufferCopy(ctx, in, len) : JS_NewStringLen(ctx, in, len);
        JSValue cb_argv[2] = {JS_DupValue(ctx, sess->ws_obj), msg};
        minnet_emit(&minnet_server.cb_message, 2, cb_argv);
        JS_FreeValue(ctx, cb_argv[0]);
        JS_FreeValue(ctx, cb_argv[1]);
      }
      return 0;
    }
    case LWS_CALLBACK_RECEIVE_PONG: {
      if(minnet_server.cb_pong.ctx) {
        // ws_obj = minnet_ws_wrap(minnet_server.cb_pong.ctx, wsi);
        JSValue msg = JS_NewArrayBufferCopy(minnet_server.cb_pong.ctx, in, len);
        JSValue cb_argv[2] = {JS_DupValue(minnet_server.cb_pong.ctx, sess->ws_obj), msg};
        minnet_emit(&minnet_server.cb_pong, 2, cb_argv);
        JS_FreeValue(minnet_server.cb_pong.ctx, cb_argv[0]);
        JS_FreeValue(minnet_server.cb_pong.ctx, cb_argv[1]);
      }
      return 0;
    }

    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL:
    case LWS_CALLBACK_ADD_POLL_FD:
    case LWS_CALLBACK_DEL_POLL_FD:
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {

      return fd_callback(wsi, reason, &minnet_server.cb_fd, in);
    }

    case LWS_CALLBACK_WSI_CREATE:
    case LWS_CALLBACK_WSI_DESTROY:
    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
    case LWS_CALLBACK_ADD_HEADERS:
    case LWS_CALLBACK_WS_SERVER_DROP_PROTOCOL: {
      return lws_callback_http_dummy(wsi, reason, user, in, len);
    }
    case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_HTTP_DROP_PROTOCOL:
    case LWS_CALLBACK_CLOSED_HTTP:
    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {
      return http_server_callback(wsi, reason, user, in, len);
    }
    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
    default: {
      minnet_lws_unhandled("WS", reason);
      return 0;
    }
  }

  lwsl_user("ws   " FG("%d") "%-25s" NC " fd=%d url='%s' in='%.*s'\n",
            22 + (reason * 2),
            lws_callback_name(reason) + 13,
            lws_get_socket_fd(wsi),
            lws_get_uri(wsi, minnet_server.ctx, WSI_TOKEN_GET_URI),
            (int)len,
            (char*)in);

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

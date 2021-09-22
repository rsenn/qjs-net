#include "minnet-server.h"
#include "minnet-websocket.h"
#include "minnet-request.h"

int
ws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  JSValue ws_obj = JS_UNDEFINED;
  MinnetSession* sess = user;

  switch(reason) {
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_PROTOCOL_INIT: return 0;

    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: {
      if(minnet_server.cb_connect.ctx) {
        JSContext* ctx = minnet_server.cb_connect.ctx;
        struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);

        opaque->req = request_new(ctx, in, lws_get_uri(wsi, ctx, WSI_TOKEN_GET_URI), METHOD_GET);

        int num_hdr = http_headers(ctx, &opaque->req->header, wsi);

        printf("ws   \033[38;5;171m%s\033[0m wsi=%p, ws=%p, req=%p, opaque=%p, num_hdr=%i, url=%s\n", lws_callback_name(reason) + 13, wsi, opaque->ws, opaque->req, opaque, num_hdr, opaque->req->url);
      }
      return 0;
    }

    case LWS_CALLBACK_ESTABLISHED: {

      if(minnet_server.cb_connect.ctx) {
        JSContext* ctx = minnet_server.cb_connect.ctx;
        struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);

if(!opaque->req)
                opaque->req = request_new(ctx, in, lws_get_uri(wsi, ctx, WSI_TOKEN_GET_URI), METHOD_GET);

        assert(opaque->req);

        if(!JS_IsObject(sess->req_obj))
          sess->req_obj = minnet_request_wrap(ctx, opaque->req);

        sess->ws_obj = minnet_ws_wrap(ctx, wsi);
        opaque->ws = minnet_ws_data(ctx, sess->ws_obj);

        printf("ws   %s wsi=%p, ws=%p, req=%p, opaque=%p\n", lws_callback_name(reason) + 13, wsi, minnet_ws_data(ctx, sess->ws_obj), opaque->req, opaque);
        minnet_emit_this(&minnet_server.cb_connect, sess->ws_obj, 2, &sess->ws_obj);
      }

      return 0;
    }

    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
    case LWS_CALLBACK_CLOSED: {
      if(!sess->closed) {
        JSContext* ctx = minnet_server.cb_close.ctx;
        JSValue why = JS_UNDEFINED;
        int code = -1;

        if(in) {
          uint8_t* codep = in;
          code = (codep[0] << 8) + codep[1];
          if(len - 2 > 0)
            why = JS_NewStringLen(minnet_server.ctx, in + 2, len - 2);
        }

        printf("ws   %-25s fd=%d\n", lws_callback_name(reason)+13, lws_get_socket_fd(wsi));

        if(ctx) {
          JSValue cb_argv[3] = {JS_DupValue(ctx, sess->ws_obj), code != -1 ? JS_NewInt32(ctx, code) : JS_UNDEFINED, why};
          minnet_emit(&minnet_server.cb_close, code != -1 ? 3 : 1, cb_argv);
          JS_FreeValue(ctx, cb_argv[0]);
          JS_FreeValue(ctx, cb_argv[1]);
        }
        JS_FreeValue(minnet_server.ctx, why);
        JS_FreeValue(minnet_server.ctx, sess->ws_obj);
        sess->ws_obj = JS_NULL;
        sess->closed = 1;
      }
      return 0;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
      /*   printf("ws   %s fd=%d\n", lws_callback_name(reason), lws_get_socket_fd(wsi));
         lws_callback_on_writable(wsi);*/
      return 0;
    }
    case LWS_CALLBACK_RECEIVE: {
      JSContext* ctx = minnet_server.cb_message.ctx;
      if(ctx) {
        MinnetWebsocket* ws = minnet_ws_data(ctx, sess->ws_obj);
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
    case LWS_CALLBACK_UNLOCK_POLL: return 0;

    case LWS_CALLBACK_ADD_POLL_FD: {
      struct lws_pollargs* args = in;

      if(minnet_server.cb_fd.ctx) {
        JSValue argv[3] = {JS_NewInt32(minnet_server.cb_fd.ctx, args->fd)};
        minnet_handlers(minnet_server.cb_fd.ctx, wsi, args, &argv[1]);

        minnet_emit(&minnet_server.cb_fd, 3, argv);

        JS_FreeValue(minnet_server.cb_fd.ctx, argv[0]);
        JS_FreeValue(minnet_server.cb_fd.ctx, argv[1]);
        JS_FreeValue(minnet_server.cb_fd.ctx, argv[2]);
      }
      return 0;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {
      struct lws_pollargs* args = in;

      if(minnet_server.cb_fd.ctx) {
        JSValue argv[3] = {
            JS_NewInt32(minnet_server.cb_fd.ctx, args->fd),
        };
        minnet_handlers(minnet_server.cb_fd.ctx, wsi, args, &argv[1]);
        minnet_emit(&minnet_server.cb_fd, 3, argv);
        JS_FreeValue(minnet_server.cb_fd.ctx, argv[0]);
        JS_FreeValue(minnet_server.cb_fd.ctx, argv[1]);
        JS_FreeValue(minnet_server.cb_fd.ctx, argv[2]);
      }
     return 0;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      struct lws_pollargs* args = in;

      if(minnet_server.cb_fd.ctx) {
        if(args->events != args->prev_events) {
          JSValue argv[3] = {JS_NewInt32(minnet_server.cb_fd.ctx, args->fd)};
          minnet_handlers(minnet_server.cb_fd.ctx, wsi, args, &argv[1]);

          minnet_emit(&minnet_server.cb_fd, 3, argv);
          JS_FreeValue(minnet_server.cb_fd.ctx, argv[0]);
          JS_FreeValue(minnet_server.cb_fd.ctx, argv[1]);
          JS_FreeValue(minnet_server.cb_fd.ctx, argv[2]);
        }
      }
     return 0;
    }

    case LWS_CALLBACK_WSI_CREATE:
    case LWS_CALLBACK_WSI_DESTROY:
    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
    case LWS_CALLBACK_ADD_HEADERS:
    case LWS_CALLBACK_WS_SERVER_DROP_PROTOCOL: {
      return 0;
    }
    case LWS_CALLBACK_CLOSED_HTTP:
    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {
      return http_callback(wsi, reason, user, in, len);
    }
    default: {
      minnet_lws_unhandled("WS", reason);
      return 0;
    }
  }

  printf("ws   %-25s fd=%d in='%.*s'\n", lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), len, in);

  return 0;
  //  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

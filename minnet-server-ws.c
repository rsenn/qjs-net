
  int
minnet_ws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  JSValue ws_obj = JS_UNDEFINED;
  MinnetSession* serv = user;
  MinnetHttpMethod method;
  char* url = lws_uri_and_method(wsi, minnet_server.cb_connect.ctx, &method);

  // printf("ws %s\tfd=%d in='%.*s'\n", lws_callback_name(reason), lws_get_socket_fd(wsi), len, in);

  switch((int)reason) {
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_PROTOCOL_INIT: return 0;

    case LWS_CALLBACK_ESTABLISHED: {

      if(minnet_server.cb_connect.ctx) {
        struct wsi_opaque_user_data* user = lws_get_opaque_user_data(wsi);

        serv->args[1] = minnet_ws_wrap(minnet_server.cb_connect.ctx, user->req);

        // printf("ws %s wsi=%p, ws=%p, req=%p, url=%s, serv=%p, user=%p\n", lws_callback_name(reason) + 13, wsi, user->ws, user->req, url, serv, lws_get_opaque_user_data(wsi));

        minnet_emit_this(&minnet_server.cb_connect, serv->args[0], 2, serv->args);
      }

      return 0;
    }

    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
    case LWS_CALLBACK_CLOSED: {
      if(!serv->closed) {
        JSValue why = JS_UNDEFINED;
        int code = -1;

        if(in) {
          uint8_t* codep = in;
          code = (codep[0] << 8) + codep[1];
          if(len - 2 > 0)
            why = JS_NewStringLen(minnet_server.ctx, in + 2, len - 2);
        }

        printf("ws %s fd=%d\n", lws_callback_name(reason), lws_get_socket_fd(wsi));

        if(minnet_server.cb_close.ctx) {
          JSValue cb_argv[3] = {JS_DupValue(minnet_server.cb_close.ctx, serv->ws_obj), code != -1 ? JS_NewInt32(minnet_server.cb_close.ctx, code) : JS_UNDEFINED, why};
          minnet_emit(&minnet_server.cb_close, code != -1 ? 3 : 1, cb_argv);
          JS_FreeValue(minnet_server.cb_close.ctx, cb_argv[0]);
          JS_FreeValue(minnet_server.cb_close.ctx, cb_argv[1]);
        }
        JS_FreeValue(minnet_server.ctx, why);
        JS_FreeValue(minnet_server.ctx, serv->ws_obj);
        serv->ws_obj = JS_NULL;
        serv->closed = 1;
      }
      return 0;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
      /*   printf("ws %s fd=%d\n", lws_callback_name(reason), lws_get_socket_fd(wsi));
         lws_callback_on_writable(wsi);*/
      return 0;
    }
    case LWS_CALLBACK_RECEIVE: {
      if(minnet_server.cb_message.ctx) {
        //  ws_obj = minnet_ws_wrap(minnet_server.cb_message.ctx, wsi);
        JSValue msg = JS_NewStringLen(minnet_server.cb_message.ctx, in, len);
        JSValue cb_argv[2] = {JS_DupValue(minnet_server.cb_message.ctx, serv->ws_obj), msg};
        minnet_emit(&minnet_server.cb_message, 2, cb_argv);
        JS_FreeValue(minnet_server.cb_message.ctx, cb_argv[0]);
        JS_FreeValue(minnet_server.cb_message.ctx, cb_argv[1]);
      }
      return 0;
    }
    case LWS_CALLBACK_RECEIVE_PONG: {
      if(minnet_server.cb_pong.ctx) {
        // ws_obj = minnet_ws_wrap(minnet_server.cb_pong.ctx, wsi);
        JSValue msg = JS_NewArrayBufferCopy(minnet_server.cb_pong.ctx, in, len);
        JSValue cb_argv[2] = {JS_DupValue(minnet_server.cb_pong.ctx, serv->ws_obj), msg};
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

    case LWS_CALLBACK_WSI_CREATE: {
      return 0;
    }
    case LWS_CALLBACK_WSI_DESTROY:
    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
    case LWS_CALLBACK_ADD_HEADERS:
    case LWS_CALLBACK_WS_SERVER_DROP_PROTOCOL: {
      return 0;
    }

    case LWS_CALLBACK_HTTP:
    case LWS_CALLBACK_HTTP_BODY:
    case LWS_CALLBACK_HTTP_BODY_COMPLETION:
    case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE:
    case LWS_CALLBACK_CLOSED_HTTP:
    case LWS_CALLBACK_FILTER_HTTP_CONNECTION:
    case LWS_CALLBACK_HTTP_DROP_PROTOCOL: {
      return minnet_http_callback(wsi, reason, user, in, len);
    }
      /*
          default: {
            minnet_lws_unhandled("WS", reason);
            return 0;
          }*/
  }
  minnet_lws_unhandled("WS", reason);
  return 0;
  //  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

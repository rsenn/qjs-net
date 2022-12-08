#include "minnet-server.h"
#include "minnet-websocket.h"
#include "minnet-request.h"
#include "jsutils.h"
#include "headers.h"
#include "minnet-response.h"
#include <alloca.h>
#include <assert.h>
#include <libwebsockets.h>

int http_server_callback(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

char* lws_hdr_simple_ptr(struct lws*, int);

int
ws_server_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  struct session_data* session = user;
  MinnetServer* server = lws_context_user(lws_get_context(wsi));
  JSContext* ctx = server->context.js;
  struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);

  if(!opaque && ctx)
    opaque = lws_opaque(wsi, ctx);

  if(lws_reason_poll(reason))
    return wsi_handle_poll(wsi, reason, &server->cb.fd, in);
  if(lws_reason_http(reason))
    return http_server_callback(wsi, reason, user, in, len);

  LOGCB("WS", "fd=%d, %s%sin='%.*s' session#%i", lws_get_socket_fd(wsi), wsi_http2(wsi) ? "h2, " : "", wsi_tls(wsi) ? "ssl, " : "", (int)len, (char*)in, session ? session->serial : 0);

  switch(reason) {
    case LWS_CALLBACK_CONNECTING: {
      break;
    }
    case LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION: {
      X509_STORE_CTX_set_error(user, X509_V_OK);
      return 0;
    }
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
    case LWS_CALLBACK_CHILD_CLOSING: {
      break;
    }
    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
      break;
    }
    case LWS_CALLBACK_PROTOCOL_INIT: {
      break;
    }
    case LWS_CALLBACK_PROTOCOL_DESTROY: {
      break;
    }

    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS:
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: {
      return lws_callback_http_dummy(wsi, reason, user, in, len);
    }
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: {

      struct lws* parent;

      if((parent = lws_get_parent(wsi))) {
        struct wsi_opaque_user_data* opaque2 = lws_get_opaque_user_data(parent);

        opaque2->upstream = wsi;
      }

      wsi_cert(wsi);
      if(!opaque->ws)
        opaque->ws = ws_new(wsi, ctx);
      return 0;
    }

    case LWS_CALLBACK_WSI_CREATE: {
      if(!opaque->ws)
        opaque->ws = ws_new(wsi, ctx);
      /*      if(session)
              session->ws_obj = minnet_ws_wrap(ctx, opaque->ws);*/
      return 0;
    }

    case LWS_CALLBACK_WSI_DESTROY: {
      /*   if(opaque && opaque->ws) {
           ws_free(opaque->ws, ctx);
           opaque->ws = 0;
         }*/
      if((opaque = lws_get_opaque_user_data(wsi))) {
        if(opaque->ws)
          opaque->ws->lwsi = 0;

        lws_set_opaque_user_data(wsi, 0);
        // opaque_free(opaque, ctx);
      }
      return 0;
    }

    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: {
      int ret = 0;
      MinnetURL url = {.protocol = protocol_string(PROTOCOL_WS)};
      url_fromwsi(&url, wsi, ctx);

      if(!wsi_tls(wsi) && !strcmp(in, "h2c")) {
        char* dest;
        size_t destlen;
        ByteBuffer out = BUFFER_0();
        url.protocol = protocol_string(PROTOCOL_HTTPS);
        dest = url_format(url, ctx);
        destlen = url_length(url);

        buffer_alloc(&out, 1024, ctx);

        if(lws_http_redirect(wsi, 308, (const unsigned char*)dest, destlen, &out.write, out.end) < 0)
          ret = -1;
        else
          ret = 1;

        url_free(&url, ctx);
        js_free(ctx, dest);
        return ret;
      }

      if(!opaque)
        opaque = lws_opaque(wsi, ctx);

      if(!opaque->req) {
        opaque->req = request_new(url, METHOD_GET, ctx);
        opaque->req->ip = wsi_ipaddr(wsi, ctx);
        opaque->req->secure = wsi_tls(wsi);
        headers_tobuffer(ctx, &opaque->req->headers, wsi);
      } else {
        url_free(&url, ctx);
      }
      return ret;
    }

    case LWS_CALLBACK_ESTABLISHED: {
      // struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);
      int status;
      status = lws_http_client_http_response(wsi);
      MinnetHttpMount* mount = 0;
      MinnetURL* url;

      if(!opaque->req) {
        opaque->req = request_fromwsi(wsi, ctx);
        opaque->req->ip = wsi_ipaddr(wsi, ctx);
      }

      if(opaque->req) {
        url = &opaque->req->url;

        if((mount = mount_find_s((MinnetHttpMount*)server->context.info.mounts, url->path))) {
          // printf("found mount mnt=%s org=%s def=%s pro=%s\n", mount->mnt, mount->org, mount->def, mount->pro);
        }

        if(!JS_IsObject(session->req_obj))
          session->req_obj = minnet_request_wrap(ctx, opaque->req);

        session->resp_obj = minnet_response_new(ctx, opaque->req->url, status, 0, TRUE, "text/html");
      }

      opaque->status = OPEN;

      if(server->cb.connect.ctx) {

        if(!JS_IsObject(session->ws_obj))
          session->ws_obj = minnet_ws_fromwsi(ctx, wsi);

        if(!opaque->ws) {
          if((opaque->ws = minnet_ws_data(session->ws_obj)))
            opaque->ws->ref_count++;
        }

        LOGCB("ws", "wsi#%" PRId64 " req=%p", opaque->serial, opaque->req);
        server_exception(server, callback_emit_this(&server->cb.connect, session->ws_obj, 2, &session->ws_obj));
      }
      return 0;
    }

    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
    case LWS_CALLBACK_CLOSED: {
      if(opaque->status < CLOSING) {
        JSValue why = JS_UNDEFINED;
        int code = -1;

        if(in) {
          uint8_t* codep = in;
          code = (codep[0] << 8) + codep[1];
          if(len - 2 > 0)
            why = JS_NewStringLen(server->context.js, (char*)in + 2, len - 2);
        }

        opaque->status = CLOSING;

        LOGCB("ws", "fd=%d, status=%d code=%d", lws_get_socket_fd(wsi), opaque->status, code);

        if(ctx) {
          JSValue cb_argv[3] = {session->ws_obj, code != -1 ? JS_NewInt32(ctx, code) : JS_UNDEFINED, why};
          server_exception(server, callback_emit(&server->cb.close, code != -1 ? 3 : 1, cb_argv));
          JS_FreeValue(ctx, cb_argv[1]);
        }
        JS_FreeValue(server->context.js, why);
        /*JS_FreeValue(server->context.js, session->ws_obj);
        session->ws_obj = JS_NULL;*/
      }
      return 0;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
      // fprintf(stderr, "\x1b[1;33mwritable\x1b[0m %s fd=%d\n", lws_callback_name(reason) + 13, lws_get_socket_fd(wsi));
      session_writable(session, opaque->binary, ctx);
      break;
    }

    case LWS_CALLBACK_RECEIVE: {
      if(ctx) {
        JSValue msg = opaque->binary ? JS_NewArrayBufferCopy(ctx, in, len) : JS_NewStringLen(ctx, in, len);
        JSValue cb_argv[2] = {JS_DupValue(ctx, session->ws_obj), msg};
        server_exception(server, callback_emit(&server->cb.message, 2, cb_argv));
        JS_FreeValue(ctx, cb_argv[0]);
        JS_FreeValue(ctx, cb_argv[1]);
      }
      return 0;
    }
    case LWS_CALLBACK_RECEIVE_PONG: {
      if(server->cb.pong.ctx) {
        // ws_obj = minnet_ws_fromwsi(server->cb.pong.ctx, wsi);
        JSValue msg = JS_NewArrayBufferCopy(server->cb.pong.ctx, in, len);
        JSValue cb_argv[2] = {JS_DupValue(server->cb.pong.ctx, session->ws_obj), msg};
        server_exception(server, callback_emit(&server->cb.pong, 2, cb_argv));
        JS_FreeValue(server->cb.pong.ctx, cb_argv[0]);
        JS_FreeValue(server->cb.pong.ctx, cb_argv[1]);
      }
      return 0;
    }
    case LWS_CALLBACK_WS_SERVER_DROP_PROTOCOL: {
      return 0;
    }
    case LWS_CALLBACK_VHOST_CERT_AGING:
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
    case LWS_CALLBACK_GET_THREAD_ID: {
      return 0;
    }
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH: break;
    case LWS_CALLBACK_CONFIRM_EXTENSION_OKAY: {
      return 0;
    }
    default: {
      // printf("ws_server_callback %s %p %p %zu\n", lws_callback_name(reason), user, in, len);
      minnet_lws_unhandled(__func__, reason);
      break;
    }
  }

  // lwsl_user("ws   " FG("%d") "%-38s" NC " fd=%d url='%s' in='%.*s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, lws_get_socket_fd(wsi), wsi_token(wsi, server->context.js, //
  // WSI_TOKEN_GET_URI), (int)len, (char*)in);

  if(opaque && opaque->status >= CLOSING)
    return -1;

  return 0; // lws_callback_http_dummy(wsi, reason, user, in, len);
}

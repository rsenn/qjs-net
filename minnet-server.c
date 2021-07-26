#include "minnet.h"
#include "minnet-websocket.h"
#include "minnet-server.h"
#include "jsutils.h"
#include "minnet-response.h"
#include "minnet-request.h"
#include <list.h>
#include <quickjs-libc.h>
#include <libwebsockets.h>

static MinnetCallback server_cb_message, server_cb_connect, server_cb_close, server_cb_pong, server_cb_fd, server_cb_http, server_cb_body;

static int callback_ws(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
static int callback_http(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

static MinnetHttpMount*
mount_create(JSContext* ctx, const char* mountpoint, const char* origin, const char* def, enum lws_mount_protocols origin_proto) {
  MinnetHttpMount* ret = js_mallocz(ctx, sizeof(MinnetHttpMount));

  ret->mountpoint = js_strdup(ctx, mountpoint);
  ret->origin = def ? js_strdup(ctx, origin) : 0;
  ret->def = def ? js_strdup(ctx, def) : 0;
  ret->protocol = "http";
  ret->origin_protocol = origin_proto;
  ret->mountpoint_len = strlen(mountpoint);

  return ret;
}

static MinnetHttpMount*
mount_new(JSContext* ctx, JSValueConst arr) {
  JSValue mnt = JS_GetPropertyUint32(ctx, arr, 0);
  JSValue org = JS_GetPropertyUint32(ctx, arr, 1);
  JSValue def = JS_GetPropertyUint32(ctx, arr, 2);
  const char* dest = JS_ToCString(ctx, org);
  const char* dotslashslash = strstr(dest, "://");
  size_t proto_len = dotslashslash ? dotslashslash - dest : 0;

  const char *mountpoint, *default_index = 0;
  MinnetHttpMount* ret = mount_create(ctx,
                                      mountpoint = JS_ToCString(ctx, mnt),
                                      &dest[proto_len ? proto_len + 3 : 0],
                                      JS_IsUndefined(def) ? 0 : (default_index = JS_ToCString(ctx, def)),
                                      proto_len == 0 ? LWSMPRO_FILE : !strncmp(dest, "https", proto_len) ? LWSMPRO_HTTPS : LWSMPRO_HTTP);

  JS_FreeCString(ctx, mountpoint);
  if(default_index)
    JS_FreeCString(ctx, default_index);

  return ret;
}

static char*
lws_get_uri(struct lws* wsi, JSContext* ctx, enum lws_token_indexes token) {
  size_t len;
  char buf[1024];

  if((len = lws_hdr_copy(wsi, buf, sizeof(buf) - 1, token)) > 0)
    buf[len] = '\0';
  else
    return 0;

  return js_strndup(ctx, buf, len);
}

static char*
lws_uri_and_method(struct lws* wsi, JSContext* ctx, char** method) {
  char* url;

  if((url = lws_get_uri(wsi, ctx, WSI_TOKEN_POST_URI)))
    *method = js_strdup(ctx, "POST");
  else if((url = lws_get_uri(wsi, ctx, WSI_TOKEN_GET_URI)))
    *method = js_strdup(ctx, "GET");

  return url;
}

static void
mount_free(JSContext* ctx, MinnetHttpMount* mount) {
  JS_FreeCString(ctx, mount->mountpoint);
  js_free(ctx, (char*)mount->origin);
  if(mount->def)
    JS_FreeCString(ctx, mount->def);
  js_free(ctx, mount);
}

static struct lws_protocols protocols[] = {
    {"minnet", callback_ws, 0, 0, 0, 0, 0},
    {"http", callback_http, 0, 0, 0, 0, 0},
    {NULL, NULL, 0, 0, 0, 0, 0},
};

static const MinnetHttpMount* mount_dyn;

JSValue
minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  int a = 0;
  int port = 7981;
  const char* host;
  struct lws_context* context;
  struct lws_context_creation_info info;

  lwsl_user("Minnet WebSocket Server\n");
  JSValue ret = JS_NewInt32(ctx, 0);
  JSValue options = argv[0];

  JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
  JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
  JSValue opt_on_pong = JS_GetPropertyStr(ctx, options, "onPong");
  JSValue opt_on_close = JS_GetPropertyStr(ctx, options, "onClose");
  JSValue opt_on_connect = JS_GetPropertyStr(ctx, options, "onConnect");
  JSValue opt_on_message = JS_GetPropertyStr(ctx, options, "onMessage");
  JSValue opt_on_fd = JS_GetPropertyStr(ctx, options, "onFd");
  JSValue opt_on_http = JS_GetPropertyStr(ctx, options, "onHttp");
  JSValue opt_on_body = JS_GetPropertyStr(ctx, options, "onBody");
  JSValue opt_mounts = JS_GetPropertyStr(ctx, options, "mounts");

  if(!JS_IsUndefined(opt_port))
    JS_ToInt32(ctx, &port, opt_port);

  if(JS_IsString(opt_host))
    host = JS_ToCString(ctx, opt_host);
  else
    host = "localhost";

  GETCB(opt_on_pong, server_cb_pong)
  GETCB(opt_on_close, server_cb_close)
  GETCB(opt_on_connect, server_cb_connect)
  GETCB(opt_on_message, server_cb_message)
  GETCB(opt_on_fd, server_cb_fd)
  GETCBTHIS(opt_on_http, server_cb_http, 0)
  GETCBTHIS(opt_on_body, server_cb_body, 0)

  SETLOG

  memset(&info, 0, sizeof info);

  protocols[0].user = ctx;
  protocols[1].user = ctx;

  info.port = port;
  info.protocols = protocols;
  info.mounts = 0;
  info.vhost_name = host;
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT /*| LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE*/;

  minnet_ws_sslcert(ctx, &info, options);
  const MinnetHttpMount** ptr = &info.mounts;

  if(JS_IsArray(ctx, opt_mounts)) {
    uint32_t i;
    for(i = 0;; i++) {
      JSValue mount = JS_GetPropertyUint32(ctx, opt_mounts, i);
      if(JS_IsUndefined(mount))
        break;
      *ptr = mount_new(ctx, mount);
      ptr = (const MinnetHttpMount**)&(*ptr)->mount_next;
    }
  }

  *ptr = mount_dyn = mount_create(ctx, "/dyn", 0, 0, LWSMPRO_CALLBACK);
  ptr = (MinnetHttpMount const**)&(*ptr)->mount_next;

  context = lws_create_context(&info);

  if(!context) {
    lwsl_err("Libwebsockets init failed\n");
    return JS_EXCEPTION;
  }

  // JSThreadState* ts = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));
  lws_service_adjust_timeout(context, 1, 0);

  while(a >= 0) {
    if(minnet_exception) {
      ret = JS_EXCEPTION;
      break;
    }

    if(server_cb_fd.func_obj)
      js_std_loop(ctx);
    else
      a = lws_service(context, 20);
  }
  lws_context_destroy(context);

  if(info.mounts) {
    const MinnetHttpMount *mount, *next;

    for(mount = info.mounts; mount; mount = next) {
      next = mount->mount_next;
      JS_FreeCString(ctx, mount->mountpoint);
      js_free(ctx, (void*)mount->origin);
      if(mount->def)
        JS_FreeCString(ctx, mount->def);
      js_free(ctx, (void*)mount);
    }
  }

  if(info.ssl_cert_filepath)
    JS_FreeCString(ctx, info.ssl_cert_filepath);

  if(info.ssl_private_key_filepath)
    JS_FreeCString(ctx, info.ssl_private_key_filepath);

  return ret;
}

static int
callback_ws(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  // JSContext* ctx = protocols[0].user;

  switch(reason) {
    case LWS_CALLBACK_PROTOCOL_INIT:
      // printf("callback PROTOCOL_INIT\n");
      break;
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_ESTABLISHED: {
      if(server_cb_connect.func_obj) {
        JSValue ws_obj = minnet_ws_object(server_cb_connect.ctx, wsi);
        minnet_emit(&server_cb_connect, 1, &ws_obj);
      }
      break;
    }
    case LWS_CALLBACK_CLOSED: {
      MinnetWebsocket* res = lws_wsi_user(wsi);

      if(server_cb_close.func_obj && (!res || res->lwsi)) {
        // printf("callback CLOSED %d\n", lws_get_socket_fd(wsi));
        JSValue ws_obj = minnet_ws_object(server_cb_close.ctx, wsi);
        JSValue cb_argv[2] = {ws_obj, in ? JS_NewStringLen(server_cb_connect.ctx, in, len) : JS_UNDEFINED};
        minnet_emit(&server_cb_close, in ? 2 : 1, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_SERVER_WRITEABLE: {
      lws_callback_on_writable(wsi);
      break;
    }
    case LWS_CALLBACK_RECEIVE: {
      if(server_cb_message.func_obj) {
        JSValue ws_obj = minnet_ws_object(server_cb_message.ctx, wsi);
        JSValue msg = JS_NewStringLen(server_cb_message.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        minnet_emit(&server_cb_message, 2, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_RECEIVE_PONG: {
      if(server_cb_pong.func_obj) {
        JSValue ws_obj = minnet_ws_object(server_cb_pong.ctx, wsi);
        JSValue msg = JS_NewArrayBufferCopy(server_cb_pong.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        minnet_emit(&server_cb_pong, 2, cb_argv);
      }
      break;
    }

    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: {
      break;
    }

    case LWS_CALLBACK_ADD_POLL_FD: {
      struct lws_pollargs* args = in;

      if(server_cb_fd.func_obj) {
        JSValue argv[3] = {JS_NewInt32(server_cb_fd.ctx, args->fd)};
        minnet_handlers(server_cb_fd.ctx, wsi, args, &argv[1]);

        minnet_emit(&server_cb_fd, 3, argv);
        JS_FreeValue(server_cb_fd.ctx, argv[0]);
        JS_FreeValue(server_cb_fd.ctx, argv[1]);
        JS_FreeValue(server_cb_fd.ctx, argv[2]);
      }
      break;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {
      struct lws_pollargs* args = in;

      if(server_cb_fd.func_obj) {
        JSValue argv[3] = {
            JS_NewInt32(server_cb_fd.ctx, args->fd),
        };
        minnet_handlers(server_cb_fd.ctx, wsi, args, &argv[1]);
        minnet_emit(&server_cb_fd, 3, argv);
        JS_FreeValue(server_cb_fd.ctx, argv[0]);
      }
      break;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      struct lws_pollargs* args = in;

      if(server_cb_fd.func_obj) {
        if(args->events != args->prev_events) {
          JSValue argv[3] = {JS_NewInt32(server_cb_fd.ctx, args->fd)};
          minnet_handlers(server_cb_fd.ctx, wsi, args, &argv[1]);

          minnet_emit(&server_cb_fd, 3, argv);
          JS_FreeValue(server_cb_fd.ctx, argv[0]);
          JS_FreeValue(server_cb_fd.ctx, argv[1]);
          JS_FreeValue(server_cb_fd.ctx, argv[2]);
        }
      }
      break;
    }
    default: {
      // lws_print_unhandled(reason);
      break;
    }
  }

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static int
callback_http(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  JSContext* ctx = server_cb_fd.ctx ? server_cb_fd.ctx : server_cb_http.ctx ? server_cb_http.ctx : server_cb_message.ctx ? server_cb_message.ctx : server_cb_connect.ctx ? server_cb_connect.ctx : 0;
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE];
  char *url = 0, *method = 0;
  JSValue ws_obj = minnet_ws_object(ctx, wsi);
  MinnetWebsocket* ws = minnet_ws_data(ctx, ws_obj);

  url = lws_uri_and_method(wsi, ctx, &method);

  switch(reason) {

    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {
      if(server_cb_http.func_obj) {
        JSValue ret, ws_obj = minnet_ws_object(server_cb_http.ctx, wsi);
        JSValue argv[] = {ws_obj, JS_NewString(server_cb_http.ctx, in)};
        int32_t result = 0;
        // MinnetWebsocket* ws = JS_GetOpaque(ws_obj, minnet_ws_class_id);
        struct byte_buffer h;
        buffer_init(&h, buf, LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE);
        printf("LWS_CALLBACK_FILTER_HTTP_CONNECTION in: %s\n", (char*)in);

        // ws->h = h;

        ret = minnet_emit(&server_cb_http, 2, argv);
        JS_FreeValue(server_cb_http.ctx, argv[0]);
        JS_FreeValue(server_cb_http.ctx, argv[1]);

        if(JS_IsNumber(ret))
          JS_ToInt32(server_cb_http.ctx, &result, ret);
        JS_FreeValue(server_cb_http.ctx, ret);

        /* if(!result) {
           if(h->pos > h->start)
             lws_finalize_write_http_header(ws->lwsi, h->start,
         &h->pos, h->end);
         }*/

        if(result)
          buffer_free(&h, JS_GetRuntime(server_cb_http.ctx));

        return result;
      }
      break;
    }
    case LWS_CALLBACK_ADD_HEADERS: {
      if(server_cb_http.func_obj) {
        // JSValue ws_obj = minnet_ws_object(server_cb_http.ctx, wsi);
        // MinnetWebsocket* ws = JS_GetOpaque(ws_obj, minnet_ws_class_id);
        struct lws_process_html_args* args = (struct lws_process_html_args*)in;

        printf("LWS_CALLBACK_ADD_HEADERS args: %.*s\n", args->len, args->p);
        /*  if(h->pos > h->start) {
            size_t len = h->pos - h->start;


            memcpy(args->p, h->start, len);
            args->p += len;
          }*/
      }

      break;
    }
    case LWS_CALLBACK_HTTP: {
      /*      JSValue req_obj = minnet_request_new(ctx, in, ws);
            MinnetRequest* req = minnet_request_data(ctx, req_obj);*/
      MinnetRequest* req;
      MinnetResponse* resp = 0;
      JSValue ret = JS_UNDEFINED, req_obj = JS_UNDEFINED, resp_obj = JS_UNDEFINED;
      MinnetBuffer b = BUFFER(buf);

      if((req = request_new(ctx))) {
        request_init(req, in, url, method);

        req_obj = minnet_request_wrap(ctx, req);
      }

      resp_obj = minnet_response_new(ctx, req->url, 200, TRUE, "text/html");
      resp = minnet_response_data(ctx, resp_obj);

      assert(req);

      ++req->ref_count;

      if(server_cb_http.func_obj) {
        JSValue args[2] = {req_obj, resp_obj};
        ret = minnet_emit(&server_cb_http, 2, args);
        if(JS_IsObject(ret) && minnet_response_data(ctx, ret)) {
          JS_FreeValue(ctx, resp_obj);
          resp_obj = ret;
          response_dump(minnet_response_data(ctx, ret));
        } else {
          JS_FreeValue(ctx, ret);
        }
      }

      resp = minnet_response_data(ctx, resp_obj);
      assert(resp);

      ws->request = JS_DupValue(ctx, req_obj);
      ws->response = JS_DupValue(ctx, resp_obj);

      /*
       * prepare and write http headers... with regards to content-
       * length, there are three approaches:
       *
       *  - http/1.0 or connection:close: no need, but no pipelining
       *  - http/1.1 or connected:keep-alive
       *     (keep-alive is default for 1.1): content-length required
       *  - http/2: no need, LWS_WRITE_HTTP_FINAL closes the stream
       *
       * giving the api below LWS_ILLEGAL_HTTP_CONTENT_LEN instead of
       * a content length forces the connection response headers to
       * send back "connection: close", disabling keep-alive.
       *
       * If you know the final content-length, it's always OK to give
       * it and keep-alive can work then if otherwise possible.  But
       * often you don't know it and avoiding having to compute it
       * at header-time makes life easier at the server.
       */
      if(lws_add_http_common_headers(wsi, resp->status, resp->type, LWS_ILLEGAL_HTTP_CONTENT_LEN, &b.pos, b.end))
        return 1;

      if(lws_finalize_write_http_header(wsi, b.start, &b.pos, b.end))
        return 1;

      if(server_cb_body.func_obj) {
        JSValueConst args[] = {req_obj, resp_obj};
        JSValue ret = minnet_emit_this(&server_cb_body, ws_obj, 2, args);

        resp->generator = ret;
      } else {
        resp->state = (MinnetHttpState){.times = 0, .content_lines = 0, .budget = 10};
      }

      lws_callback_on_writable(wsi);
      JS_FreeValue(ctx, req_obj);
      JS_FreeValue(ctx, resp_obj);

      return 0;
    }
    case LWS_CALLBACK_HTTP_WRITEABLE: {
      MinnetResponse* res = minnet_response_data(ctx, ws->response);
      enum lws_write_protocol n = LWS_WRITE_HTTP;
      JSValue ret = JS_UNDEFINED;
      MinnetBuffer b = BUFFER(buf);

      if(server_cb_body.func_obj) {
        BOOL done = FALSE;
        JSValue next = JS_UNDEFINED;
        // printf("res->iterator %s\n", JS_ToCString(ctx, res->iterator));
        JSValue ret = js_iterator_next(server_cb_body.ctx, res->generator, &next, &done, 0, 0);

        if(JS_IsException(ret)) {
          JSValue exception = JS_GetException(ctx);

          fprintf(stderr, "Exception: %s\n", JS_ToCString(ctx, exception));
          n = LWS_WRITE_HTTP_FINAL;
        } else if(!done) {
          if(1 || JS_IsString(ret)) {
            size_t len;
            const char* str = JS_ToCStringLen(server_cb_body.ctx, &len, ret);
            buffer_append(&b, str, len);
            res->body.pos += len;

            if(len && str[len - 1] == '\n')
              len--;

            // printf("Data: %.*s\n", (int)len, str);

            JS_FreeCString(ctx, str);
          }

        } else {
          n = LWS_WRITE_HTTP_FINAL;
        }
      } else {

        if(!res || res->state.times > res->state.budget)
          break;

        if(res->state.times == res->state.budget)
          n = LWS_WRITE_HTTP_FINAL;

        if(!res->state.times) {
          /*
           * to work with http/2, we must take care about LWS_PRE
           * valid behind the buffer we will send.
           */
          buffer_printf(&b,
                        "<html>\n"
                        "  <head>\n"
                        "   <meta charset=utf-8 http-equiv=\"Content-Language\" content=\"en\"/>\n"
                        " </head>\n"
                        " <state>\n"
                        "    <img src=\"/libwebsockets.org-logo.svg\"><br />\n"
                        "   Dynamic content for '%s' from mountpoint.<br />\n"
                        "   <br />\n",
                        res->url); // ((MinnetRequest*)((char*)res - offsetof(struct http_request, rsp)))->path);
        } else if(res->state.times < res->state.budget) {
          /*
           * after the first time, we create bulk content.
           *
           * Again we take care about LWS_PRE valid behind the
           * buffer we will send.
           */

          while(buffer_OFFSET(&b) < 300) { buffer_printf(&b, "%d.%d: this is some content...<br />\n", res->state.times, res->state.content_lines++); }

        } else {
          buffer_printf(&b,
                        "<br />"
                        "</state>\n"
                        "</html>\n");
        }

        res->state.times++;
      }

      if(lws_write(wsi, buffer_START(&b), buffer_OFFSET(&b), n) != buffer_OFFSET(&b))
        return 1;

      /*
       * HTTP/1.0 no keepalive: close network connection
       * HTTP/1.1 or HTTP1.0 + KA: wait / process next transaction
       * HTTP/2: stream ended, parent connection remains up
       */
      if(n == LWS_WRITE_HTTP_FINAL) {
        if(lws_http_transaction_completed(wsi))
          return -1;
      } else {
        lws_callback_on_writable(wsi);
      }

      return 0;
    }
    default: {
      // lws_print_unhandled(reason);
      break;
    }
  }
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

#include "minnet.h"
#include "minnet-websocket.h"
#include "minnet-server.h"
#include "minnet-jsutils.h"
#include "minnet-response.h"
#include "list.h"
#include "quickjs-libc.h"

static MinnetWebsocketCallback server_cb_message, server_cb_connect, server_cb_close, server_cb_pong, server_cb_fd, server_cb_http;

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

  len = lws_hdr_copy(wsi, buf, sizeof(buf) - 1, token);
  buf[len] = '\0';

  return js_strndup(ctx, buf, len);
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
  GETCB(opt_on_http, server_cb_http)

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
        minnet_ws_emit(&server_cb_connect, 1, &ws_obj);
      }
      break;
    }
    case LWS_CALLBACK_CLOSED: {
      MinnetWebsocket* res = lws_wsi_user(wsi);

      if(server_cb_close.func_obj && (!res || res->lwsi)) {
        // printf("callback CLOSED %d\n", lws_get_socket_fd(wsi));
        JSValue ws_obj = minnet_ws_object(server_cb_close.ctx, wsi);
        JSValue cb_argv[2] = {ws_obj, in ? JS_NewStringLen(server_cb_connect.ctx, in, len) : JS_UNDEFINED};
        minnet_ws_emit(&server_cb_close, in ? 2 : 1, cb_argv);
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
        minnet_ws_emit(&server_cb_message, 2, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_RECEIVE_PONG: {
      if(server_cb_pong.func_obj) {
        JSValue ws_obj = minnet_ws_object(server_cb_pong.ctx, wsi);
        JSValue msg = JS_NewArrayBufferCopy(server_cb_pong.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        minnet_ws_emit(&server_cb_pong, 2, cb_argv);
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

        minnet_ws_emit(&server_cb_fd, 3, argv);
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
        minnet_ws_emit(&server_cb_fd, 3, argv);
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

          minnet_ws_emit(&server_cb_fd, 3, argv);
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

  time_t t;
#if defined(LWS_HAVE_CTIME_R)
  char date[32];
#endif
  // MinnetBuffer* h = &r->h;

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

        ret = minnet_ws_emit(&server_cb_http, 2, argv);
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
          buffer_free(server_cb_http.ctx, &h);

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
      MinnetRequest* req = minnet_request_new(ctx, in, wsi);
      uint8_t *start = &buf[LWS_PRE], *p = start, *end = &buf[sizeof(buf) - 1];

      lws_set_wsi_user(wsi, req);
      /*
       * If you want to know the full url path used, you can get it
       * like this
       *
       * n = lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_GET_URI);
       *
       * The base path is the first (n - strlen((const char *)in))
       * chars in buf.
       */

      /*
       * In contains the url part after the place the mount was
       * positioned at, eg, if positioned at "/dyn" and given
       * "/dyn/mypath", in will contain /mypath
       */
      lws_snprintf(req->path, sizeof(req->path), "%s", (const char*)in);

      lws_get_peer_simple(wsi, (char*)buf, sizeof(buf));
      lwsl_notice("%s: HTTP: connection %s, path %s\n", __func__, (const char*)buf, req->path);

      /*
       * Demonstrates how to retreive a urlarg x=value
       */

      {
        char value[100];
        int z = lws_get_urlarg_by_name_safe(wsi, "x", value, sizeof(value) - 1);

        if(z >= 0)
          lwsl_hexdump_notice(value, (size_t)z);
      }

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
      if(lws_add_http_common_headers(wsi,
                                     HTTP_STATUS_OK,
                                     "text/html",
                                     LWS_ILLEGAL_HTTP_CONTENT_LEN, /* no content len */
                                     &p,
                                     end))
        return 1;
      if(lws_finalize_write_http_header(wsi, start, &p, end))
        return 1;

      req->times = 0;
      req->budget = atoi((char*)in + 1);
      req->content_lines = 0;
      if(!req->budget)
        req->budget = 10;

      /* write the body separately */
      lws_callback_on_writable(wsi);

      return 0;
    }
    case LWS_CALLBACK_HTTP_WRITEABLE: {
      MinnetRequest* req = lws_wsi_user(wsi);
      MinnetBuffer b = {&buf[LWS_PRE], &buf[LWS_PRE], &buf[sizeof(buf) - 1]};
      enum lws_write_protocol n;

      if(!req || req->times > req->budget)
        break;

      n = LWS_WRITE_HTTP;
      if(req->times == req->budget)
        n = LWS_WRITE_HTTP_FINAL;

      if(!req->times) {
        /*
         * the first time, we print some html title
         */
        t = time(NULL);
        /*
         * to work with http/2, we must take care about LWS_PRE
         * valid behind the buffer we will send.
         */
        b.pos += lws_snprintf((char*)b.pos,
                              lws_ptr_diff_size_t(b.end, b.pos),
                              "<html>"
                              "<head><meta charset=utf-8 "
                              "http-equiv=\"Content-Language\" "
                              "content=\"en\"/></head><body>"
                              "<img src=\"/libwebsockets.org-logo.svg\">"
                              "<br>Dynamic content for '%s' from mountpoint."
                              "<br>Time: %s<br><br>"
                              "</body></html>",
                              req->path,
#if defined(LWS_HAVE_CTIME_R)
                              ctime_r(&t, date));
#else
                              ctime(&t));
#endif
      } else {
        /*
         * after the first time, we create bulk content.
         *
         * Again we take care about LWS_PRE valid behind the
         * buffer we will send.
         */

        while(lws_ptr_diff(b.end, b.pos) > 80) b.pos += lws_snprintf((char*)b.pos, lws_ptr_diff_size_t(b.end, b.pos), "%d.%d: this is some content... ", req->times, req->content_lines++);

        b.pos += lws_snprintf((char*)b.pos, lws_ptr_diff_size_t(b.end, b.pos), "<br><br>");
      }

      req->times++;
      if(lws_write(wsi, b.start, lws_ptr_diff_size_t(b.pos, b.start), n) != lws_ptr_diff(b.pos, b.start))
        return 1;

      /*
       * HTTP/1.0 no keepalive: close network connection
       * HTTP/1.1 or HTTP1.0 + KA: wait / process next transaction
       * HTTP/2: stream ended, parent connection remains up
       */
      if(n == LWS_WRITE_HTTP_FINAL) {
        if(lws_http_transaction_completed(wsi))
          return -1;
      } else
        lws_callback_on_writable(wsi);

      return 0;
    }
    default: {
      // lws_print_unhandled(reason);
      break;
    }
  }
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

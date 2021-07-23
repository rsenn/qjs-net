#include "minnet.h"
#include "websocket.h"
#include "server.h"
#include "list.h"

static struct minnet_ws_callback server_cb_message;
static struct minnet_ws_callback server_cb_connect;
static struct minnet_ws_callback server_cb_error;
static struct minnet_ws_callback server_cb_close;
static struct minnet_ws_callback server_cb_pong;
static struct minnet_ws_callback server_cb_fd;
static struct minnet_ws_callback server_cb_http;

/*
 * Unlike ws, http is a stateless protocol.  This pss only exists for the
 * duration of a single http transaction.  With http/1.1 keep-alive and http/2,
 * that is unrelated to (shorter than) the lifetime of the network connection.
 */
struct pss {
  char path[128];

  int times;
  int budget;

  int content_lines;
};

static int interrupted;

static int
lws_http_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  struct pss* pss = (struct pss*)user;
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE], *start = &buf[LWS_PRE], *p = start, *end = &buf[sizeof(buf) - 1];
  time_t t;
  int n;
#if defined(LWS_HAVE_CTIME_R)
  char date[32];
#endif
  struct http_header* header = 0;

  switch(reason) {

    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {
      if(server_cb_http.func_obj) {
        // printf("callback FILTER_HTTP_CONNECTION %s\n", in);
        JSValue ret, ws_obj = get_websocket_obj(server_cb_http.ctx, wsi);
        JSValue argv[] = {ws_obj, JS_NewString(server_cb_http.ctx, in)};
        int32_t result = 0;
        MinnetWebsocket* ws = JS_GetOpaque(ws_obj, minnet_ws_class_id);
        if(!(header = ws->header))
          header = ws->header = js_mallocz(server_cb_http.ctx, sizeof(struct http_header));

        http_header_alloc(server_cb_http.ctx, &ws->header, LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE);

        ret = call_websocket_callback(&server_cb_http, 2, argv);
        JS_FreeValue(server_cb_http.ctx, argv[0]);
        JS_FreeValue(server_cb_http.ctx, argv[1]);

        if(JS_IsNumber(ret))
          JS_ToInt32(server_cb_http.ctx, &result, ret);
        JS_FreeValue(server_cb_http.ctx, ret);

        /* if(!result) {
           if(header->pos > header->start)
             lws_finalize_write_http_header(ws->lwsi, header->start,
         &header->pos, header->end);
         }*/

        if(result)
          http_header_free(server_cb_http.ctx, &ws->header);

        //   if(result)
        return result;
      }
      break;
    }
    case LWS_CALLBACK_ADD_HEADERS: {
      if(server_cb_http.func_obj) {
        JSValue ws_obj = get_websocket_obj(server_cb_http.ctx, wsi);
        MinnetWebsocket* ws = JS_GetOpaque(ws_obj, minnet_ws_class_id);
        struct lws_process_html_args* args = (struct lws_process_html_args*)in;
        if(!(header = ws->header))
          header = ws->header = js_mallocz(server_cb_http.ctx, sizeof(struct http_header));

        if(header->pos > header->start) {
          size_t len = header->pos - header->start;

          // assert(len <= args->max_len);

          memcpy(args->p, header->start, len);
          args->p += len;
        }
      }

      break;
    }
    case LWS_CALLBACK_HTTP: {

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
      lws_snprintf(pss->path, sizeof(pss->path), "%s", (const char*)in);

      lws_get_peer_simple(wsi, (char*)buf, sizeof(buf));
      lwsl_notice("%s: HTTP: connection %s, path %s\n", __func__, (const char*)buf, pss->path);

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

      pss->times = 0;
      pss->budget = atoi((char*)in + 1);
      pss->content_lines = 0;
      if(!pss->budget)
        pss->budget = 10;

      /* write the body separately */
      lws_callback_on_writable(wsi);

      return 0;
    }
    case LWS_CALLBACK_HTTP_WRITEABLE: {

      if(!pss || pss->times > pss->budget)
        break;

      n = LWS_WRITE_HTTP;
      if(pss->times == pss->budget)
        n = LWS_WRITE_HTTP_FINAL;

      if(!pss->times) {
        /*
         * the first time, we print some html title
         */
        t = time(NULL);
        /*
         * to work with http/2, we must take care about LWS_PRE
         * valid behind the buffer we will send.
         */
        p += lws_snprintf((char*)p,
                          lws_ptr_diff_size_t(end, p),
                          "<html>"
                          "<head><meta charset=utf-8 "
                          "http-equiv=\"Content-Language\" "
                          "content=\"en\"/></head><body>"
                          "<img src=\"/libwebsockets.org-logo.svg\">"
                          "<br>Dynamic content for '%s' from mountpoint."
                          "<br>Time: %s<br><br>"
                          "</body></html>",
                          pss->path,
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

        while(lws_ptr_diff(end, p) > 80)
          p += lws_snprintf(
              (char*)p, lws_ptr_diff_size_t(end, p), "%d.%d: this is some content... ", pss->times, pss->content_lines++);

        p += lws_snprintf((char*)p, lws_ptr_diff_size_t(end, p), "<br><br>");
      }

      pss->times++;
      if(lws_write(wsi, (uint8_t*)start, lws_ptr_diff_size_t(p, start), (enum lws_write_protocol)n) != lws_ptr_diff(p, start))
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
      lws_print_unhandled(reason);
      break;
    }
  }
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static int
lws_ws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  // JSContext* ctx = lws_server_protocols[0].user;

  switch(reason) {
    case LWS_CALLBACK_PROTOCOL_INIT:
      // printf("callback PROTOCOL_INIT\n");
      break;
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_ESTABLISHED: {
      if(server_cb_connect.func_obj) {
        JSValue ws_obj = get_websocket_obj(server_cb_connect.ctx, wsi);
        call_websocket_callback(&server_cb_connect, 1, &ws_obj);
      }
      break;
    }
    case LWS_CALLBACK_CLOSED: {
      MinnetWebsocket* res = lws_wsi_user(wsi);

      if(server_cb_close.func_obj && (!res || res->lwsi)) {
        // printf("callback CLOSED %d\n", lws_get_socket_fd(wsi));
        JSValue ws_obj = get_websocket_obj(server_cb_close.ctx, wsi);
        JSValue cb_argv[2] = {ws_obj, in ? JS_NewStringLen(server_cb_connect.ctx, in, len) : JS_UNDEFINED};
        call_websocket_callback(&server_cb_close, in ? 2 : 1, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_SERVER_WRITEABLE: {
      lws_callback_on_writable(wsi);
      break;
    }
    case LWS_CALLBACK_RECEIVE: {
      if(server_cb_message.func_obj) {
        JSValue ws_obj = get_websocket_obj(server_cb_message.ctx, wsi);
        JSValue msg = JS_NewStringLen(server_cb_message.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        call_websocket_callback(&server_cb_message, 2, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_RECEIVE_PONG: {
      if(server_cb_pong.func_obj) {
        JSValue ws_obj = get_websocket_obj(server_cb_pong.ctx, wsi);
        JSValue msg = JS_NewArrayBufferCopy(server_cb_pong.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        call_websocket_callback(&server_cb_pong, 2, cb_argv);
      }
      break;
    }

    case LWS_CALLBACK_ADD_POLL_FD: {
      struct lws_pollargs* args = in;
      // printf("callback ADD_POLL_FD fd=%d events=%s %s %s\n", args->fd,
      // (args->events & POLLIN) ? "IN" : "", (args->events &
      // POLLOUT) ? "OUT" : "", (args->events & POLLERR) ? "ERR" : "");
      if(server_cb_fd.func_obj) {
        JSValue argv[3] = {JS_NewInt32(server_cb_fd.ctx, args->fd)};
        minnet_handlers(server_cb_fd.ctx, wsi, args, &argv[1]);

        call_websocket_callback(&server_cb_fd, 3, argv);
        JS_FreeValue(server_cb_fd.ctx, argv[0]);
        JS_FreeValue(server_cb_fd.ctx, argv[1]);
        JS_FreeValue(server_cb_fd.ctx, argv[2]);
      }
      break;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {
      struct lws_pollargs* args = in;
      // printf("callback DEL_POLL_FD fd=%d\n", args->fd);
      JSValue argv[3] = {
          JS_NewInt32(server_cb_fd.ctx, args->fd),
      };
      minnet_handlers(server_cb_fd.ctx, wsi, args, &argv[1]);
      call_websocket_callback(&server_cb_fd, 3, argv);
      JS_FreeValue(server_cb_fd.ctx, argv[0]);

      break;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      struct lws_pollargs* args = in;

      // printf("callback CHANGE_MODE_POLL_FD fd=%d events=%03o
      // prev_events=%03o\n", args->fd, args->events, args->prev_events);

      if(args->events != args->prev_events) {
        JSValue argv[3] = {JS_NewInt32(server_cb_fd.ctx, args->fd)};
        minnet_handlers(server_cb_fd.ctx, wsi, args, &argv[1]);

        call_websocket_callback(&server_cb_fd, 3, argv);
        JS_FreeValue(server_cb_fd.ctx, argv[0]);
        JS_FreeValue(server_cb_fd.ctx, argv[1]);
        JS_FreeValue(server_cb_fd.ctx, argv[2]);
      }
      break;
    }

      /*
       case LWS_CALLBACK_WSI_DESTROY: {
         // printf("callback LWS_CALLBACK_WSI_DESTROY %d\n",
         // lws_get_socket_fd(in));
         break;
       }*/

    default: {
      //  lws_print_unhandled(reason);
      break;
    }
  }
  // return lws_http_callback(wsi, reason, user, in, len);

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static struct lws_http_mount*
http_mount_get(JSContext* ctx, JSValueConst arr) {
  JSValue mountpoint = JS_GetPropertyUint32(ctx, arr, 0);
  JSValue origin = JS_GetPropertyUint32(ctx, arr, 1);
  JSValue def = JS_GetPropertyUint32(ctx, arr, 2);
  struct lws_http_mount* ret = js_mallocz(ctx, sizeof(struct lws_http_mount));
  const char* dest = JS_ToCString(ctx, origin);
  const char* dotslashslash = strstr(dest, "://");
  size_t proto_len = dotslashslash ? dotslashslash - dest : 0;

  ret->mountpoint = JS_ToCString(ctx, mountpoint);
  ret->origin = js_strdup(ctx, &dest[proto_len ? proto_len + 3 : 0]);
  ret->def = JS_IsUndefined(def) ? 0 : JS_ToCString(ctx, def);
  ret->origin_protocol = proto_len == 0 ? LWSMPRO_FILE : !strncmp(dest, "https", proto_len) ? LWSMPRO_HTTPS : LWSMPRO_HTTP;
  ret->mountpoint_len = strlen(ret->mountpoint);

  JS_FreeCString(ctx, dest);

  return ret;
}

static void
http_mount_free(JSContext* ctx, struct lws_http_mount* mount) {
  JS_FreeCString(ctx, mount->mountpoint);
  js_free(ctx, (char*)mount->origin);
  if(mount->def)
    JS_FreeCString(ctx, mount->def);
  js_free(ctx, mount);
}

typedef struct JSThreadState {
  struct list_head os_rw_handlers;
  struct list_head os_signal_handlers;
  struct list_head os_timers;
  struct list_head port_list;
  int eval_script_recurse;
  void *recv_pipe, *send_pipe;
} JSThreadState;

static struct lws_protocols lws_server_protocols[] = {
    {"minnet", lws_ws_callback, 0, 0},
    {"http", lws_http_callback, 0, 0},
    {NULL, NULL, 0, 0},
};

static const struct lws_http_mount mount_dyn = {
    /* .mount_next */ NULL,   /* linked-list "next" */
    /* .mountpoint */ "/dyn", /* mountpoint URL */
    /* .origin */ NULL,       /* protocol */
    /* .def */ NULL,
    /* .protocol */ "http",
    /* .cgienv */ NULL,
    /* .extra_mimetypes */ NULL,
    /* .interpret */ NULL,
    /* .cgi_timeout */ 0,
    /* .cache_max_age */ 0,
    /* .auth_mask */ 0,
    /* .cache_reusable */ 0,
    /* .cache_revalidate */ 0,
    /* .cache_intermediaries */ 0,
    /* .origin_protocol */ LWSMPRO_CALLBACK, /* dynamic */
    /* .mountpoint_len */ 4,                 /* char count */
    /* .basic_auth_login_file */ NULL,
};

JSValue
minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  int a = 0;
  int port = 7981;
  const char* host;
  struct lws_context* context;
  struct lws_context_creation_info info;

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

  lws_server_protocols[0].user = ctx;
  lws_server_protocols[1].user = ctx;

  info.port = port;
  info.protocols = lws_server_protocols;
  info.mounts = 0;
  info.vhost_name = host;
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT /*| LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE*/;

  minnet_ws_sslcert(ctx, &info, options);
  const struct lws_http_mount** ptr = &info.mounts;

  if(JS_IsArray(ctx, opt_mounts)) {
    uint32_t i;

    for(i = 0;; i++) {
      JSValue mount = JS_GetPropertyUint32(ctx, opt_mounts, i);

      if(JS_IsUndefined(mount))
        break;

      *ptr = http_mount_get(ctx, mount);
      ptr = (const struct lws_http_mount**)&(*ptr)->mount_next;
    }
  }

  *ptr = &mount_dyn;
  ptr = &mount_dyn.mount_next;

  context = lws_create_context(&info);

  if(!context) {
    lwsl_err("Libwebsockets init failed\n");
    return JS_EXCEPTION;
  }

  JSThreadState* ts = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));
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
    const struct lws_http_mount *mount, *next;

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
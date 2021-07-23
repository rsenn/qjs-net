#include "minnet.h"
#include "list.h"
#include <assert.h>
#include <curl/curl.h>
#include <sys/time.h>

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

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

static void
lws_log_callback(int level, const char* line) {
  if(minnet_log_ctx) {
    if(JS_VALUE_GET_TAG(minnet_log) == 0 && JS_VALUE_GET_TAG(minnet_log_this) == 0)
      get_console_log(minnet_log_ctx, &minnet_log_this, &minnet_log);

    if(JS_IsFunction(minnet_log_ctx, minnet_log)) {
      size_t len = strlen(line);
      JSValueConst argv[2] = {JS_NewString(minnet_log_ctx, "minnet"),
                              JS_NewStringLen(minnet_log_ctx, line, len > 0 && line[len - 1] == '\n' ? len - 1 : len)};
      JSValue ret = JS_Call(minnet_log_ctx, minnet_log, minnet_log_this, 2, argv);

      if(JS_IsException(ret))
        minnet_exception = TRUE;

      JS_FreeValue(minnet_log_ctx, argv[0]);
      JS_FreeValue(minnet_log_ctx, argv[1]);
      JS_FreeValue(minnet_log_ctx, ret);
    }
  }
}

__attribute__((visibility("default"))) JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_minnet_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));

  // Add class Response
  JS_NewClassID(&minnet_response_class_id);
  JS_NewClass(JS_GetRuntime(ctx), minnet_response_class_id, &minnet_response_class);
  JSValue response_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, response_proto, minnet_response_proto_funcs, countof(minnet_response_proto_funcs));
  JS_SetClassProto(ctx, minnet_response_class_id, response_proto);

  // Add class WebSocket
  JS_NewClassID(&minnet_ws_class_id);
  JS_NewClass(JS_GetRuntime(ctx), minnet_ws_class_id, &minnet_ws_class);
  JSValue websocket_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, websocket_proto, minnet_ws_proto_funcs, countof(minnet_ws_proto_funcs));
  JS_SetClassProto(ctx, minnet_ws_class_id, websocket_proto);

  minnet_log_ctx = ctx;

  lws_set_log_level(LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE, lws_log_callback);

  return m;
}

#define GETCB(opt, cb_ptr)                                                                                                     \
  if(JS_IsFunction(ctx, opt)) {                                                                                                \
    struct minnet_ws_callback cb = {ctx, &this_val, &opt};                                                                     \
    cb_ptr = cb;                                                                                                               \
  }
#define SETLOG lws_set_log_level(LLL_ERR, NULL);

static JSValue
minnet_service_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
  int32_t rw = 0;
  uint32_t calls = ++func_data[3].u.int32;
  struct lws_pollfd pfd;
  struct lws_pollargs args = *(struct lws_pollargs*)&JS_VALUE_GET_PTR(func_data[4]);
  struct lws_context* context = JS_VALUE_GET_PTR(func_data[2]);

  if(argc >= 1)
    JS_ToInt32(ctx, &rw, argv[0]);

  pfd.fd = JS_VALUE_GET_INT(func_data[0]);
  pfd.revents = rw ? POLLOUT : POLLIN;
  pfd.events = JS_VALUE_GET_INT(func_data[1]);

  if(pfd.events != (POLLIN | POLLOUT) || poll(&pfd, 1, 0) > 0)
    lws_service_fd(context, &pfd);

  /*if (calls <= 100)
    printf("minnet %s handler calls=%i fd=%d events=%d revents=%d pfd=[%d "
         "%d %d]\n",
         rw ? "writable" : "readable", calls, pfd.fd, pfd.events,
         pfd.revents, args.fd, args.events, args.prev_events);*/

  return JS_UNDEFINED;
}

enum { READ_HANDLER = 0, WRITE_HANDLER };

static JSValue
minnet_make_handler(JSContext* ctx, struct lws_pollargs* pfd, struct lws* wsi, int magic) {
  JSValue data[5] = {
      JS_MKVAL(JS_TAG_INT, pfd->fd),
      JS_MKVAL(JS_TAG_INT, pfd->events),
      JS_MKPTR(0, lws_get_context(wsi)),
      JS_MKVAL(JS_TAG_INT, 0),
      JS_MKPTR(0, *(void**)pfd),
  };

  return JS_NewCFunctionData(ctx, minnet_service_handler, 0, magic, countof(data), data);
}

static JSValue
minnet_function_bound(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue* func_data) {
  JSValue args[argc + magic];
  size_t i, j;
  for(i = 0; i < magic; i++) args[i] = func_data[i + 1];
  for(j = 0; j < argc; j++) args[i++] = argv[j];

  return JS_Call(ctx, func_data[0], this_val, i, args);
}

static JSValue
minnet_function_bind(JSContext* ctx, JSValueConst func, int argc, JSValueConst argv[]) {
  JSValue data[argc + 1];
  size_t i;
  data[0] = JS_DupValue(ctx, func);
  for(i = 0; i < argc; i++) data[i + 1] = JS_DupValue(ctx, argv[i]);
  return JS_NewCFunctionData(ctx, minnet_function_bound, 0, argc, argc + 1, data);
}

static JSValue
minnet_function_bind_1(JSContext* ctx, JSValueConst func, JSValueConst arg) {
  return minnet_function_bind(ctx, func, 1, &arg);
}

static void
minnet_make_handlers(JSContext* ctx, struct lws* wsi, struct lws_pollargs* pfd, JSValue out[2]) {
  JSValue func = minnet_make_handler(ctx, pfd, wsi, 0);

  out[0] = (pfd->events & POLLIN) ? minnet_function_bind_1(ctx, func, JS_NewInt32(ctx, READ_HANDLER)) : JS_NULL;
  out[1] = (pfd->events & POLLOUT) ? minnet_function_bind_1(ctx, func, JS_NewInt32(ctx, WRITE_HANDLER)) : JS_NULL;

  JS_FreeValue(ctx, func);
}

static int lws_ws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

static int lws_http_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

static struct lws_protocols lws_server_protocols[] = {
    {"minnet", lws_ws_callback, 0, 0},
    {"http", lws_http_callback, 0, 0},
    {NULL, NULL, 0, 0},
};

static int
lws_http_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  struct pss* pss = (struct pss*)user;
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE], *start = &buf[LWS_PRE], *p = start, *end = &buf[sizeof(buf) - 1];
  time_t t;
  int n;
#if defined(LWS_HAVE_CTIME_R)
  char date[32];
#endif

  switch(reason) {

    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {
      if(server_cb_http.func_obj) {
        // printf("callback FILTER_HTTP_CONNECTION %s\n", in);
        JSValue ret, ws_obj = get_websocket_obj(server_cb_http.ctx, wsi);
        JSValue argv[] = {ws_obj, JS_NewString(server_cb_http.ctx, in)};
        int32_t result = 0;
        MinnetWebsocket* ws = JS_GetOpaque(ws_obj, minnet_ws_class_id);

        http_header_alloc(server_cb_http.ctx, &ws->header, LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE);

        ret = call_ws_callback(&server_cb_http, 2, argv);
        JS_FreeValue(server_cb_http.ctx, argv[0]);
        JS_FreeValue(server_cb_http.ctx, argv[1]);

        if(JS_IsNumber(ret))
          JS_ToInt32(server_cb_http.ctx, &result, ret);
        JS_FreeValue(server_cb_http.ctx, ret);

        /* if(!result) {
           if(ws->header.pos > ws->header.start)
             lws_finalize_write_http_header(ws->lwsi, ws->header.start,
         &ws->header.pos, ws->header.end);
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

        if(ws->header.pos > ws->header.start) {
          size_t len = ws->header.pos - ws->header.start;

          assert(len <= args->max_len);

          memcpy(args->p, ws->header.start, len);
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
      minnet_print_unhandled(reason);
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
        call_ws_callback(&server_cb_connect, 1, &ws_obj);
      }
      break;
    }
    case LWS_CALLBACK_CLOSED: {
      MinnetWebsocket* res = lws_wsi_user(wsi);

      if(server_cb_close.func_obj && (!res || res->lwsi)) {
        // printf("callback CLOSED %d\n", lws_get_socket_fd(wsi));
        JSValue ws_obj = get_websocket_obj(server_cb_close.ctx, wsi);
        JSValue cb_argv[2] = {ws_obj, in ? JS_NewStringLen(server_cb_connect.ctx, in, len) : JS_UNDEFINED};
        call_ws_callback(&server_cb_close, in ? 2 : 1, cb_argv);
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
        call_ws_callback(&server_cb_message, 2, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_RECEIVE_PONG: {
      if(server_cb_pong.func_obj) {
        JSValue ws_obj = get_websocket_obj(server_cb_pong.ctx, wsi);
        JSValue msg = JS_NewArrayBufferCopy(server_cb_pong.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        call_ws_callback(&server_cb_pong, 2, cb_argv);
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
        minnet_make_handlers(server_cb_fd.ctx, wsi, args, &argv[1]);

        call_ws_callback(&server_cb_fd, 3, argv);
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
      minnet_make_handlers(server_cb_fd.ctx, wsi, args, &argv[1]);
      call_ws_callback(&server_cb_fd, 3, argv);
      JS_FreeValue(server_cb_fd.ctx, argv[0]);

      break;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      struct lws_pollargs* args = in;

      // printf("callback CHANGE_MODE_POLL_FD fd=%d events=%03o
      // prev_events=%03o\n", args->fd, args->events, args->prev_events);

      if(args->events != args->prev_events) {
        JSValue argv[3] = {JS_NewInt32(server_cb_fd.ctx, args->fd)};
        minnet_make_handlers(server_cb_fd.ctx, wsi, args, &argv[1]);

        call_ws_callback(&server_cb_fd, 3, argv);
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
      //  minnet_print_unhandled(reason);
      break;
    }
  }
  // return lws_http_callback(wsi, reason, user, in, len);

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static struct lws_http_mount*
minnet_get_mount(JSContext* ctx, JSValueConst arr) {
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
minnet_free_mount(JSContext* ctx, struct lws_http_mount* mount) {
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

static void
minnet_ws_sslcert(JSContext* ctx, struct lws_context_creation_info* info, JSValueConst options) {
  JSValue opt_ssl_cert = JS_GetPropertyStr(ctx, options, "sslCert");
  JSValue opt_ssl_private_key = JS_GetPropertyStr(ctx, options, "sslPrivateKey");
  JSValue opt_ssl_ca = JS_GetPropertyStr(ctx, options, "sslCA");

  if(JS_IsString(opt_ssl_cert))
    info->ssl_cert_filepath = JS_ToCString(ctx, opt_ssl_cert);
  if(JS_IsString(opt_ssl_private_key))
    info->ssl_private_key_filepath = JS_ToCString(ctx, opt_ssl_private_key);
  if(JS_IsString(opt_ssl_ca))
    info->client_ssl_ca_filepath = JS_ToCString(ctx, opt_ssl_ca);
}
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

static JSValue
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

      *ptr = minnet_get_mount(ctx, mount);
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

static struct lws_context* client_context;
static struct lws* client_wsi;
static int client_server_port = 7981;
static const char* client_server_address = "localhost";

static int
connect_client(void) {
  struct lws_client_connect_info i;

  memset(&i, 0, sizeof(i));

  i.context = client_context;
  i.port = client_server_port;
  i.address = client_server_address;
  i.path = "/";
  i.host = i.address;
  i.origin = i.address;
  i.ssl_connection = 0;
  i.protocol = "minnet";
  i.pwsi = &client_wsi;

  return !lws_client_connect_via_info(&i);
}

static int
lws_client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  switch(reason) {
    case LWS_CALLBACK_PROTOCOL_INIT: {
      // connect_client();
      break;
    }
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: {
      break;
    }
    case LWS_CALLBACK_CONNECTING: {
      //   printf("LWS_CALLBACK_CONNECTING\n");
      break;
    }

    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
      break;
    }
    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {

      break;
    }
    case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      client_wsi = NULL;
      if(client_cb_close.func_obj) {
        JSValue why = in ? JS_NewString(client_cb_close.ctx, in) : JS_NULL;
        call_ws_callback(&client_cb_close, 1, &why);
      }
      break;
    }
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_CLIENT_ESTABLISHED: {
      if(client_cb_connect.func_obj) {
        JSValue ws_obj = get_websocket_obj(client_cb_connect.ctx, wsi);
        call_ws_callback(&client_cb_connect, 1, &ws_obj);
      }
      break;
    }
    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      lws_callback_on_writable(wsi);
      break;
    }
    case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL: {
      client_wsi = NULL;
      if(client_cb_close.func_obj) {
        JSValue cb_argv[] = {get_websocket_obj(client_cb_close.ctx, wsi), JS_NewString(client_cb_close.ctx, in)};
        call_ws_callback(&client_cb_close, 2, cb_argv);
        JS_FreeValue(client_cb_close.ctx, cb_argv[0]);
        JS_FreeValue(client_cb_close.ctx, cb_argv[1]);
      }
      break;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE: {
      if(client_cb_message.func_obj) {
        JSValue ws_obj = get_websocket_obj(client_cb_message.ctx, wsi);
        JSValue msg = JS_NewStringLen(client_cb_message.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        call_ws_callback(&client_cb_message, 2, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
      if(client_cb_pong.func_obj) {
        JSValue ws_obj = get_websocket_obj(client_cb_pong.ctx, wsi);
        JSValue data = JS_NewArrayBufferCopy(client_cb_pong.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, data};
        call_ws_callback(&client_cb_pong, 2, cb_argv);
      }
      break;
    }
    case LWS_CALLBACK_ADD_POLL_FD: {
      struct lws_pollargs* args = in;
      if(client_cb_fd.func_obj) {
        JSValue argv[3] = {JS_NewInt32(client_cb_fd.ctx, args->fd)};
        minnet_make_handlers(client_cb_fd.ctx, wsi, args, &argv[1]);

        call_ws_callback(&client_cb_fd, 3, argv);
        JS_FreeValue(client_cb_fd.ctx, argv[0]);
        JS_FreeValue(client_cb_fd.ctx, argv[1]);
        JS_FreeValue(client_cb_fd.ctx, argv[2]);
      }
      break;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {
      struct lws_pollargs* args = in;
      if(client_cb_fd.func_obj) {
        JSValue argv[3] = {
            JS_NewInt32(client_cb_fd.ctx, args->fd),
        };
        minnet_make_handlers(client_cb_fd.ctx, wsi, args, &argv[1]);
        call_ws_callback(&client_cb_fd, 3, argv);
        JS_FreeValue(client_cb_fd.ctx, argv[0]);
      }
      break;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      struct lws_pollargs* args = in;

      if(client_cb_fd.func_obj && args->events != args->prev_events) {
        JSValue argv[3] = {JS_NewInt32(client_cb_fd.ctx, args->fd)};
        minnet_make_handlers(client_cb_fd.ctx, wsi, args, &argv[1]);

        call_ws_callback(&client_cb_fd, 3, argv);
        JS_FreeValue(client_cb_fd.ctx, argv[0]);
        JS_FreeValue(client_cb_fd.ctx, argv[1]);
        JS_FreeValue(client_cb_fd.ctx, argv[2]);
      }
      break;
    }

    default: {
      // minnet_print_unhandled(reason);
      break;
    }
  }

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static const struct lws_protocols client_protocols[] = {
    {"minnet", lws_client_callback, 0, 0},
    {NULL, NULL, 0, 0},
};

static JSValue
minnet_ws_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  struct lws_context_creation_info info;
  int n = 0;
  JSValue ret = JS_NewInt32(ctx, 0);

  SETLOG

  memset(&info, 0, sizeof info);
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = client_protocols;

  JSValue options = argv[0];
  JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
  JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
  JSValue opt_on_pong = JS_GetPropertyStr(ctx, options, "onPong");
  JSValue opt_on_close = JS_GetPropertyStr(ctx, options, "onClose");
  JSValue opt_on_connect = JS_GetPropertyStr(ctx, options, "onConnect");
  JSValue opt_on_message = JS_GetPropertyStr(ctx, options, "onMessage");
  JSValue opt_on_fd = JS_GetPropertyStr(ctx, options, "onFd");

  if(JS_IsString(opt_host))
    client_server_address = JS_ToCString(ctx, opt_host);

  if(JS_IsNumber(opt_port))
    JS_ToInt32(ctx, &client_server_port, opt_port);

  GETCB(opt_on_pong, client_cb_pong)
  GETCB(opt_on_close, client_cb_close)
  GETCB(opt_on_connect, client_cb_connect)
  GETCB(opt_on_message, client_cb_message)
  GETCB(opt_on_fd, client_cb_fd)

  minnet_ws_sslcert(ctx, &info, options);

  client_context = lws_create_context(&info);
  if(!client_context) {
    lwsl_err("Libwebsockets init failed\n");
    return JS_EXCEPTION;
  }

  connect_client();
  minnet_exception = FALSE;

  while(n >= 0) {
    if(minnet_exception) {
      ret = JS_EXCEPTION;
      break;
    }

    if(client_cb_fd.func_obj)
      js_std_loop(ctx);
    else
      /* n =*/lws_service(client_context, 500);
  }

  lws_context_destroy(client_context);

  return ret;
}

static JSValue
minnet_ws_send(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetWebsocket* ws_obj;
  const char* msg;
  uint8_t* data;
  size_t len;
  int m, n;

  if(!(ws_obj = JS_GetOpaque2(ctx, this_val, minnet_ws_class_id)))
    return JS_EXCEPTION;

  if(JS_IsString(argv[0])) {
    msg = JS_ToCString(ctx, argv[0]);
    len = strlen(msg);
    uint8_t buffer[LWS_PRE + len];

    n = lws_snprintf((char*)&buffer[LWS_PRE], len + 1, "%s", msg);
    m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_TEXT);
    if(m < n) {
      // Sending message failed
      return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
  }

  data = JS_GetArrayBuffer(ctx, &len, argv[0]);
  if(data) {
    uint8_t buffer[LWS_PRE + len];
    memcpy(&buffer[LWS_PRE], data, len);

    m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_BINARY);
    if(m < len) {
      // Sending data failed
      return JS_EXCEPTION;
    }
  }
  return JS_UNDEFINED;
}

static JSValue
minnet_ws_respond(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  MinnetWebsocket* ws_obj;
  JSValue ret = JS_UNDEFINED;
  struct http_header* header;

  if(!(ws_obj = JS_GetOpaque2(ctx, this_val, minnet_ws_class_id)))
    return JS_EXCEPTION;

  header = &ws_obj->header;

  switch(magic) {
    case 0: {
      const char* msg = 0;
      uint32_t status = 0;

      JS_ToUint32(ctx, &status, argv[0]);

      if(argc >= 2)
        msg = JS_ToCString(ctx, argv[1]);

      lws_return_http_status(ws_obj->lwsi, status, msg);
      if(msg)
        JS_FreeCString(ctx, msg);
      break;
    }
    case 1: {

      const char* msg = 0;
      size_t len = 0;
      uint32_t status = 0;

      JS_ToUint32(ctx, &status, argv[0]);

      if(argc >= 2)
        msg = JS_ToCStringLen(ctx, &len, argv[1]);

      if(lws_http_redirect(ws_obj->lwsi, status, (unsigned char*)msg, len, &header->pos, header->end) < 0)
        ret = JS_NewInt32(ctx, -1);
      if(msg)
        JS_FreeCString(ctx, msg);
      break;
    }
    case 2: {
      size_t namelen;
      const char* namestr = JS_ToCStringLen(ctx, &namelen, argv[0]);
      char* name = js_malloc(ctx, namelen + 2);
      size_t len;
      const char* value = JS_ToCStringLen(ctx, &len, argv[1]);

      memcpy(name, namestr, namelen);
      name[namelen] = ':';
      name[namelen + 1] = '\0';

      if(lws_add_http_header_by_name(ws_obj->lwsi, name, value, len, &header->pos, header->end) < 0)
        ret = JS_NewInt32(ctx, -1);

      js_free(ctx, name);
      JS_FreeCString(ctx, namestr);
      JS_FreeCString(ctx, value);
      break;
    }
  }

  return ret;
}

static JSValue
minnet_ws_ping(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetWebsocket* ws_obj;
  uint8_t* data;
  size_t len;

  if(!(ws_obj = JS_GetOpaque2(ctx, this_val, minnet_ws_class_id)))
    return JS_EXCEPTION;

  data = JS_GetArrayBuffer(ctx, &len, argv[0]);
  if(data) {
    uint8_t buffer[len + LWS_PRE];
    memcpy(&buffer[LWS_PRE], data, len);

    int m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_PING);
    if(m < len) {
      // Sending ping failed
      return JS_EXCEPTION;
    }
  } else {
    uint8_t buffer[LWS_PRE];
    lws_write(ws_obj->lwsi, &buffer[LWS_PRE], 0, LWS_WRITE_PING);
  }
  return JS_UNDEFINED;
}

static JSValue
minnet_ws_pong(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetWebsocket* ws_obj;
  uint8_t* data;
  size_t len;

  if(!(ws_obj = JS_GetOpaque2(ctx, this_val, minnet_ws_class_id)))
    return JS_EXCEPTION;

  data = JS_GetArrayBuffer(ctx, &len, argv[0]);
  if(data) {
    uint8_t buffer[len + LWS_PRE];
    memcpy(&buffer[LWS_PRE], data, len);

    int m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_PONG);
    if(m < len) {
      // Sending pong failed
      return JS_EXCEPTION;
    }
  } else {
    uint8_t buffer[LWS_PRE];
    lws_write(ws_obj->lwsi, &buffer[LWS_PRE], 0, LWS_WRITE_PONG);
  }
  return JS_UNDEFINED;
}

static JSValue
minnet_ws_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetWebsocket* ws_obj;
  const char* reason = 0;
  size_t rlen = 0;

  if(!(ws_obj = JS_GetOpaque2(ctx, this_val, minnet_ws_class_id)))
    return JS_EXCEPTION;

  if(ws_obj->lwsi) {
    int optind = 0;
    uint32_t status = LWS_CLOSE_STATUS_NORMAL;

    if(optind < argc && JS_IsNumber(argv[optind]))
      JS_ToInt32(ctx, &status, argv[optind++]);

    if(optind < argc) {
      reason = JS_ToCStringLen(ctx, &rlen, argv[optind++]);
      if(rlen > 124)
        rlen = 124;
    }

    if(reason)
      lws_close_reason(ws_obj->lwsi, status, reason, rlen);

    lws_close_free_wsi(ws_obj->lwsi, status, "minnet_ws_close");

    ws_obj->lwsi = 0;
    return JS_TRUE;
  }

  return JS_FALSE;
}

static JSValue
minnet_ws_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetWebsocket* ws_obj;
  JSValue ret = JS_UNDEFINED;
  if(!(ws_obj = JS_GetOpaque2(ctx, this_val, minnet_ws_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case 0: {
      ret = JS_NewInt32(ctx, lws_get_socket_fd(ws_obj->lwsi));
      break;
    }
    case 1: {
      char address[1024];
      lws_get_peer_simple(ws_obj->lwsi, address, sizeof(address));

      ret = JS_NewString(ctx, address);
      break;
    }
    case 2:
    case 3: {
      struct sockaddr_in addr;
      socklen_t addrlen = sizeof(addr);
      int fd = lws_get_socket_fd(ws_obj->lwsi);

      if(getpeername(fd, &addr, &addrlen) != -1) {
        ret = JS_NewInt32(ctx, magic == 2 ? addr.sin_family : addr.sin_port);
      }
      break;
    }
    case 4: {
      struct sockaddr_in addr;
      socklen_t addrlen = sizeof(addr);
      int fd = lws_get_socket_fd(ws_obj->lwsi);

      if(getpeername(fd, &addr, &addrlen) != -1) {
        ret = JS_NewArrayBufferCopy(ctx, &addr, addrlen);
      }
      break;
    }
  }
  return ret;
}

static void
minnet_ws_finalizer(JSRuntime* rt, JSValue val) {
  MinnetWebsocket* ws_obj = JS_GetOpaque(val, minnet_ws_class_id);
  if(ws_obj) {
    if(--ws_obj->ref_count == 0)
      js_free_rt(rt, ws_obj);
  }
}

static JSValue
minnet_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  CURL* curl;
  CURLcode curlRes;
  const char* url;
  FILE* fi;
  MinnetResponse* res;
  uint8_t* buffer;
  long bufSize;
  long status;
  char* type;
  const char* body_str = NULL;
  struct curl_slist* headerlist = NULL;
  char* buf = calloc(1, 1);
  size_t bufsize = 1;

  JSValue resObj = JS_NewObjectClass(ctx, minnet_response_class_id);
  if(JS_IsException(resObj))
    return JS_EXCEPTION;

  res = js_mallocz(ctx, sizeof(*res));

  if(!res) {
    JS_FreeValue(ctx, resObj);
    return JS_EXCEPTION;
  }

  if(!JS_IsString(argv[0]))
    return JS_EXCEPTION;

  res->url = argv[0];
  url = JS_ToCString(ctx, argv[0]);

  if(argc > 1 && JS_IsObject(argv[1])) {
    JSValue method, body, headers;
    const char* method_str;
    method = JS_GetPropertyStr(ctx, argv[1], "method");
    body = JS_GetPropertyStr(ctx, argv[1], "body");
    headers = JS_GetPropertyStr(ctx, argv[1], "headers");

    if(!JS_IsUndefined(headers)) {
      JSValue global_obj, object_ctor, /* object_proto, */ keys, names, length;
      int i;
      int32_t len;

      global_obj = JS_GetGlobalObject(ctx);
      object_ctor = JS_GetPropertyStr(ctx, global_obj, "Object");
      keys = JS_GetPropertyStr(ctx, object_ctor, "keys");

      names = JS_Call(ctx, keys, object_ctor, 1, (JSValueConst*)&headers);
      length = JS_GetPropertyStr(ctx, names, "length");

      JS_ToInt32(ctx, &len, length);

      for(i = 0; i < len; i++) {
        char* h;
        JSValue key, value;
        const char *key_str, *value_str;
        size_t key_len, value_len;
        key = JS_GetPropertyUint32(ctx, names, i);
        key_str = JS_ToCString(ctx, key);
        key_len = strlen(key_str);

        value = JS_GetPropertyStr(ctx, headers, key_str);
        value_str = JS_ToCString(ctx, value);
        value_len = strlen(value_str);

        buf = realloc(buf, bufsize + key_len + 2 + value_len + 2 + 1);
        h = &buf[bufsize];

        strcpy(&buf[bufsize], key_str);
        bufsize += key_len;
        strcpy(&buf[bufsize], ": ");
        bufsize += 2;
        strcpy(&buf[bufsize], value_str);
        bufsize += value_len;
        strcpy(&buf[bufsize], "\0\n");
        bufsize += 2;

        JS_FreeCString(ctx, key_str);
        JS_FreeCString(ctx, value_str);

        headerlist = curl_slist_append(headerlist, h);
      }

      JS_FreeValue(ctx, global_obj);
      JS_FreeValue(ctx, object_ctor);
      // JS_FreeValue(ctx, object_proto);
      JS_FreeValue(ctx, keys);
      JS_FreeValue(ctx, names);
      JS_FreeValue(ctx, length);
    }

    method_str = JS_ToCString(ctx, method);

    if(!JS_IsUndefined(body) || !strcasecmp(method_str, "post")) {
      body_str = JS_ToCString(ctx, body);
    }

    JS_FreeCString(ctx, method_str);

    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, body);
    JS_FreeValue(ctx, headers);
  }

  curl = curl_easy_init();
  if(!curl)
    return JS_EXCEPTION;

  fi = tmpfile();

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "minimal-network-quickjs");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fi);

  if(body_str)
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str);

  curlRes = curl_easy_perform(curl);
  if(curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK)
    res->status = JS_NewInt32(ctx, (int32_t)status);

  if(curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type) == CURLE_OK)
    res->type = type ? JS_NewString(ctx, type) : JS_NULL;

  res->ok = JS_FALSE;

  if(curlRes != CURLE_OK) {
    fprintf(stderr, "CURL failed: %s\n", curl_easy_strerror(curlRes));
    goto finish;
  }

  bufSize = ftell(fi);
  rewind(fi);

  buffer = calloc(1, bufSize + 1);
  if(!buffer) {
    fclose(fi), fputs("memory alloc fails", stderr);
    goto finish;
  }

  /* copy the file into the buffer */
  if(1 != fread(buffer, bufSize, 1, fi)) {
    fclose(fi), free(buffer), fputs("entire read fails", stderr);
    goto finish;
  }

  fclose(fi);

  res->ok = JS_TRUE;
  res->buffer = buffer;
  res->size = bufSize;

finish:
  curl_slist_free_all(headerlist);
  free(buf);
  if(body_str)
    JS_FreeCString(ctx, body_str);

  curl_easy_cleanup(curl);
  JS_SetOpaque(resObj, res);

  return resObj;
}

static JSValue
minnet_response_buffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res && res->buffer) {
    JSValue val = JS_NewArrayBufferCopy(ctx, res->buffer, res->size);
    return val;
  }

  return JS_EXCEPTION;
}

static JSValue
minnet_response_json(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res && res->buffer)
    return JS_ParseJSON(ctx, (char*)res->buffer, res->size, "<input>");

  return JS_EXCEPTION;
}

static JSValue
minnet_response_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res && res->buffer)
    return JS_NewStringLen(ctx, (char*)res->buffer, res->size);

  return JS_EXCEPTION;
}

static JSValue
minnet_response_getter_ok(JSContext* ctx, JSValueConst this_val) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return res->ok;

  return JS_EXCEPTION;
}

static JSValue
minnet_response_getter_url(JSContext* ctx, JSValueConst this_val) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return res->url;

  return JS_EXCEPTION;
}

static JSValue
minnet_response_getter_status(JSContext* ctx, JSValueConst this_val) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return res->status;

  return JS_EXCEPTION;
}

static JSValue
minnet_response_getter_type(JSContext* ctx, JSValueConst this_val) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res) {
    return res->type;
  }

  return JS_EXCEPTION;
}

static void
minnet_response_finalizer(JSRuntime* rt, JSValue val) {
  MinnetResponse* res = JS_GetOpaque(val, minnet_response_class_id);
  if(res) {
    if(res->buffer)
      free(res->buffer);
    js_free_rt(rt, res);
  }
}

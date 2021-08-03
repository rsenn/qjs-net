#include "minnet.h"
#include "minnet-websocket.h"
#include "minnet-server.h"
#include "jsutils.h"
#include "minnet-response.h"
#include "minnet-request.h"
#include <list.h>
#include <quickjs-libc.h>
#include <libwebsockets.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static MinnetHttpServer server;

static int callback_ws(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
static int callback_http(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

/**
 * @brief      Create HTTP server mount
 */
static MinnetHttpMount*
mount_create(JSContext* ctx, const char* mountpoint, const char* origin, const char* def, enum lws_mount_protocols origin_proto) {
  MinnetHttpMount* m = js_mallocz(ctx, sizeof(MinnetHttpMount));

  /*printf("mount_create mnt=%s org=%s def=%s\n", mountpoint, origin, def);*/

  m->lws.mountpoint = js_strdup(ctx, mountpoint);
  m->lws.origin = origin ? js_strdup(ctx, origin) : 0;
  m->lws.def = def ? js_strdup(ctx, def) : 0;
  m->lws.protocol = "http";
  m->lws.origin_protocol = origin_proto;
  m->lws.mountpoint_len = strlen(mountpoint);

  return m;
}

static MinnetHttpMount*
mount_new(JSContext* ctx, JSValueConst arr) {
  MinnetHttpMount* ret;
  JSValue mnt = JS_GetPropertyUint32(ctx, arr, 0);
  JSValue org = JS_GetPropertyUint32(ctx, arr, 1);
  JSValue def = JS_GetPropertyUint32(ctx, arr, 2);
  const char* path = JS_ToCString(ctx, mnt);

  if(JS_IsFunction(ctx, org)) {
    ret = mount_create(ctx, path, 0, 0, LWSMPRO_CALLBACK);

    GETCBTHIS(org, ret->callback, JS_UNDEFINED);

  } else {
    const char* dest = JS_ToCString(ctx, org);
    const char* dotslashslash = strstr(dest, "://");
    size_t plen = dotslashslash ? dotslashslash - dest : 0;
    const char* origin = &dest[plen ? plen + 3 : 0];
    const char* index = JS_IsUndefined(def) ? 0 : JS_ToCString(ctx, def);
    enum lws_mount_protocols proto = plen == 0 ? LWSMPRO_FILE : !strncmp(dest, "https", plen) ? LWSMPRO_HTTPS : LWSMPRO_HTTP;

    ret = mount_create(ctx, path, origin, index, proto);

    if(index)
      JS_FreeCString(ctx, index);
    JS_FreeCString(ctx, dest);
  }

  JS_FreeCString(ctx, path);

  JS_FreeValue(ctx, mnt);
  JS_FreeValue(ctx, org);
  JS_FreeValue(ctx, def);

  return ret;
}

static MinnetHttpMount const*
mount_find(const char* x, size_t n) {
  MinnetHttpMount const *ptr, *m = 0;
  size_t l = 0;
  if(n == 0)
    n = strlen(x);
  if(x[0] == '/') {
    x++;
    n--;
  }
  for(ptr = (MinnetHttpMount const*)&server.info.mounts; ptr; ptr = ptr->next) {
    if(ptr->lws.origin_protocol == LWSMPRO_CALLBACK) {
      const char* mnt = ptr->lws.mountpoint;
      size_t len = ptr->lws.mountpoint_len;
      if(mnt[0] == '/') {
        mnt++;
        len--;
      }
      // printf("m %.*s\n", (int)len, mnt);
      if(n >= len && len >= l && !strncmp(mnt, x, MIN(len, n))) {
        m = ptr;
        l = len;
      }
    }
  }
  return m;
}

static void
mount_free(JSContext* ctx, MinnetHttpMount const* m) {
  js_free(ctx, (void*)m->lws.mountpoint);

  if(m->lws.origin)
    js_free(ctx, (void*)m->lws.origin);

  if(m->lws.def)
    js_free(ctx, m->lws.def);

  js_free(ctx, (void*)m);
}

static struct lws_protocols protocols[] = {
    {"minnet", callback_ws, sizeof(MinnetWebsocket), 0, 0, 0, 1024},
    {"http", callback_http, sizeof(MinnetWebsocket), 0, 0, 0, 1024},
    LWS_PROTOCOL_LIST_TERM,
};

// static const MinnetHttpMount* mount_dyn;

JSValue
minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  int a = 0;
  int port = 7981;
  memset(&server, 0, sizeof server);

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
  // JSValue opt_on_body = JS_GetPropertyStr(ctx, options, "onBody");
  JSValue opt_mounts = JS_GetPropertyStr(ctx, options, "mounts");

  if(!JS_IsUndefined(opt_port))
    JS_ToInt32(ctx, &port, opt_port);

  if(JS_IsString(opt_host))
    server.info.vhost_name = js_to_string(ctx, opt_host);
  else
    server.info.vhost_name = js_strdup(ctx, "localhost");

  GETCB(opt_on_pong, server.cb_pong)
  GETCB(opt_on_close, server.cb_close)
  GETCB(opt_on_connect, server.cb_connect)
  GETCB(opt_on_message, server.cb_message)
  GETCB(opt_on_fd, server.cb_fd)
  GETCB(opt_on_http, server.cb_http)

  protocols[0].user = ctx;
  protocols[1].user = ctx;

  server.ctx = ctx;
  server.info.port = port;
  server.info.protocols = protocols;
  server.info.mounts = 0;
  server.info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT /*| LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE*/;

  minnet_ws_sslcert(ctx, &server.info, options);

  if(JS_IsArray(ctx, opt_mounts)) {
    MinnetHttpMount** ptr = (MinnetHttpMount**)&server.info.mounts;
    uint32_t i;

    for(i = 0;; i++) {
      JSValue mount = JS_GetPropertyUint32(ctx, opt_mounts, i);

      if(JS_IsUndefined(mount))
        break;

      ADD(ptr, mount_new(ctx, mount), next);

      /**ptr = mount_new(ctx, mount);
      ptr = (MinnetHttpMount const**)&(*ptr)->next;*/
    }
  }

  if(!(server.context = lws_create_context(&server.info))) {
    lwsl_err("Libwebsockets init failed\n");
    return JS_EXCEPTION;
  }

  lws_service_adjust_timeout(server.context, 1, 0);

  while(a >= 0) {
    if(minnet_exception) {
      ret = JS_EXCEPTION;
      break;
    }

    if(server.cb_fd.ctx)
      js_std_loop(ctx);
    else
      a = lws_service(server.context, 20);
  }

  lws_context_destroy(server.context);

  if(server.info.mounts) {
    const MinnetHttpMount *mount, *next;

    for(mount = (MinnetHttpMount*)server.info.mounts; mount; mount = next) {
      next = (MinnetHttpMount*)mount->lws.mount_next;
      mount_free(ctx, mount);
    }
  }

  if(server.info.ssl_cert_filepath)
    JS_FreeCString(ctx, server.info.ssl_cert_filepath);

  if(server.info.ssl_private_key_filepath)
    JS_FreeCString(ctx, server.info.ssl_private_key_filepath);

  js_free(ctx, (void*)server.info.vhost_name);

  FREECB(server.cb_pong)
  FREECB(server.cb_close)
  FREECB(server.cb_connect)
  FREECB(server.cb_message)
  FREECB(server.cb_fd)
  FREECB(server.cb_http)

  return ret;
}

static int
callback_ws(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  JSValue ws_obj = JS_UNDEFINED;
  MinnetWebsocket* ws = 0;

  switch(reason) {
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
    case LWS_CALLBACK_PROTOCOL_INIT: return 0;

    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_ESTABLISHED: {
      if(server.cb_connect.ctx) {
        ws_obj = minnet_ws_object(server.cb_connect.ctx, wsi);
        minnet_emit(&server.cb_connect, 1, &ws_obj);
      }
      return 0;
    }
    case LWS_CALLBACK_CLOSED: {
      MinnetWebsocket* res = lws_wsi_user(wsi);

      if(server.cb_close.ctx && (!res || res->lwsi)) {
        ws_obj = minnet_ws_object(server.cb_close.ctx, wsi);
        JSValue cb_argv[2] = {ws_obj, in ? JS_NewStringLen(server.cb_connect.ctx, in, len) : JS_UNDEFINED};
        minnet_emit(&server.cb_close, in ? 2 : 1, cb_argv);
      }
      return 0;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
      printf("lws_callback_on_writable %d\n", lws_get_socket_fd(wsi));
      lws_callback_on_writable(wsi);
      return 0;
    }
    case LWS_CALLBACK_RECEIVE: {
      if(server.cb_message.ctx) {
        ws_obj = minnet_ws_object(server.cb_message.ctx, wsi);
        JSValue msg = JS_NewStringLen(server.cb_message.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        minnet_emit(&server.cb_message, 2, cb_argv);
      }
      return 0;
    }
    case LWS_CALLBACK_RECEIVE_PONG: {
      if(server.cb_pong.ctx) {
        ws_obj = minnet_ws_object(server.cb_pong.ctx, wsi);
        JSValue msg = JS_NewArrayBufferCopy(server.cb_pong.ctx, in, len);
        JSValue cb_argv[2] = {ws_obj, msg};
        minnet_emit(&server.cb_pong, 2, cb_argv);
      }
      return 0;
    }

    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: return 0;

    case LWS_CALLBACK_ADD_POLL_FD: {
      struct lws_pollargs* args = in;

      if(server.cb_fd.ctx) {

        ws_obj = minnet_ws_object(server.cb_fd.ctx, wsi);
        JSValue argv[3] = {JS_NewInt32(server.cb_fd.ctx, args->fd)};
        minnet_handlers(server.cb_fd.ctx, wsi, args, &argv[1]);

        minnet_emit(&server.cb_fd, 3, argv);

        JS_FreeValue(server.cb_fd.ctx, argv[0]);
        JS_FreeValue(server.cb_fd.ctx, argv[1]);
        JS_FreeValue(server.cb_fd.ctx, argv[2]);
      }
      return 0;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {
      struct lws_pollargs* args = in;

      if(server.cb_fd.ctx) {
        JSValue argv[3] = {
            JS_NewInt32(server.cb_fd.ctx, args->fd),
        };
        minnet_handlers(server.cb_fd.ctx, wsi, args, &argv[1]);
        minnet_emit(&server.cb_fd, 3, argv);
        JS_FreeValue(server.cb_fd.ctx, argv[0]);
      }
      return 0;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      struct lws_pollargs* args = in;

      if(server.cb_fd.ctx) {
        if(args->events != args->prev_events) {
          JSValue argv[3] = {JS_NewInt32(server.cb_fd.ctx, args->fd)};
          minnet_handlers(server.cb_fd.ctx, wsi, args, &argv[1]);

          minnet_emit(&server.cb_fd, 3, argv);
          JS_FreeValue(server.cb_fd.ctx, argv[0]);
          JS_FreeValue(server.cb_fd.ctx, argv[1]);
          JS_FreeValue(server.cb_fd.ctx, argv[2]);
        }
      }
      return 0;
    }

    case LWS_CALLBACK_WSI_CREATE:
    case LWS_CALLBACK_WSI_DESTROY:
    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION: {
      return 0;
    }

    case LWS_CALLBACK_HTTP:
    case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE:
    case LWS_CALLBACK_CLOSED_HTTP:
    case LWS_CALLBACK_FILTER_HTTP_CONNECTION:
    case LWS_CALLBACK_HTTP_DROP_PROTOCOL: {
      return callback_http(wsi, reason, user, in, len);
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

static int
callback_http(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  JSContext* ctx = server.ctx;
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE];
  char *url = 0, *method = 0;
  JSValue ws_obj = minnet_ws_object(ctx, wsi);
  MinnetWebsocket* ws = minnet_ws_data(ctx, ws_obj);

  url = lws_uri_and_method(wsi, ctx, &method);

  switch(reason) {
    case LWS_CALLBACK_CHECK_ACCESS_RIGHTS: // return 0;
    case LWS_CALLBACK_PROTOCOL_INIT:
    case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_HTTP_DROP_PROTOCOL:
    case LWS_CALLBACK_CLOSED_HTTP:
    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE:

    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {

      break;
    }

    case LWS_CALLBACK_ADD_HEADERS: {
      /*struct lws_process_html_args* args = (struct lws_process_html_args*)in;

      printf("LWS_CALLBACK_ADD_HEADERS args: %.*s\n", args->len, args->p);*/
      break;
    }

    case LWS_CALLBACK_HTTP: {
      MinnetHttpMount const* mount;
      MinnetCallback const* cb = &server.cb_body;
      MinnetBuffer b = BUFFER(buf);
      JSValue ret = JS_UNDEFINED, args[2] = {minnet_request_new(ctx, in, url, method), minnet_response_new(ctx, url, 200, TRUE, "text/html")};
      MinnetRequest* req = minnet_request_data(ctx, args[0]);
      MinnetResponse* resp = minnet_response_data(ctx, args[1]);

      if((mount = mount_find(len ? in : url, len))) {

        int tok, len;

        buffer_alloc(&req->header, 1024, ctx);
        for(tok = WSI_TOKEN_HOST; tok < WSI_TOKEN_COUNT; tok++) {

          if((len = lws_hdr_total_length(wsi, tok)) > 0) {
            char hdr[len + 1];
            lws_hdr_copy(wsi, hdr, len + 1, tok);
            hdr[len] = '\0';
            // printf("HEADER %s %s\n", lws_token_to_string(tok), hdr);
            for(;;) {
              if(buffer_printf(&req->header, "%s %s\n", lws_token_to_string(tok), hdr))
                break;
              buffer_grow(&req->header, 1024, ctx);
            }
          }
        }

        ws_obj = minnet_ws_object(ctx, wsi);
        ws = minnet_ws_data(ctx, ws_obj);

        cb = &mount->callback;
        server.cb_body = *cb;

        printf("LWS_CALLBACK_HTTP url='%s'\n", resp->url);

        assert(req);

        ++req->ref_count;

        if(server.cb_http.ctx) {
          ret = minnet_emit_this(&server.cb_http, ws_obj, 2, args);
          if(JS_IsObject(ret) && minnet_response_data(ctx, ret)) {
            JS_FreeValue(ctx, args[1]);
            args[1] = ret;
            resp = minnet_response_data(ctx, ret);
            response_dump(resp);
          } else {
            JS_FreeValue(ctx, ret);
          }
        }

        assert(resp);
        assert(ws);

        resp->read_only = TRUE;

        ws->request = JS_DupValue(ctx, args[0]);
        ws->response = JS_DupValue(ctx, args[1]);

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
        if(lws_add_http_common_headers(wsi, resp->status, resp->type, LWS_ILLEGAL_HTTP_CONTENT_LEN, &b.wrpos, b.end))
          return 1;

        {
          struct list_head* el;

          list_for_each(el, &resp->headers) {
            struct http_header* hdr = list_entry(el, struct http_header, link);

            if((lws_add_http_header_by_name(wsi, (const unsigned char*)hdr->name, (const unsigned char*)hdr->value, strlen(hdr->value), &b.wrpos, b.end)))
              JS_ThrowInternalError(server.cb_body.ctx, "lws_add_http_header_by_name failed");
          }
        }

        if(lws_finalize_write_http_header(wsi, b.start, &b.wrpos, b.end)) {
          JS_FreeValue(ctx, args[0]);
          JS_FreeValue(ctx, args[1]);
          JS_FreeValue(ctx, ws_obj);
          return 1;
        }

        {

          if(cb && cb->ctx) {
            JSValue ret = minnet_emit_this(cb, ws_obj, 2, args);

            resp->generator = ret;

          } else {
            if(lws_http_transaction_completed(wsi))
              return -1;
          }
          printf("lws_callback_on_writable %d\n", lws_get_socket_fd(wsi));
          lws_callback_on_writable(wsi);
        }

        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
        JS_FreeValue(ctx, ws_obj);
      }
      /*    else {
            // lws_callback_on_writable(wsi);
            server.cb_body.ctx = 0;
            break;
          }*/

      return 0;
    }
    case LWS_CALLBACK_HTTP_WRITEABLE: {
      if(server.cb_body.ctx == 0)
        break;

      MinnetResponse* res = minnet_response_data(server.cb_body.ctx, ws->response);
      enum lws_write_protocol n = LWS_WRITE_HTTP;
      JSValue ret = JS_UNDEFINED;
      BOOL done = FALSE;

      /* if(!res->body.start)
         buffer_alloc(&res->body, LWS_RECOMMENDED_MIN_HEADER_SPACE, server.cb_body.ctx);*/

      if(server.cb_body.ctx) {
        JSValue next = JS_UNDEFINED;
        JSValue ret;

        ret = js_iterator_next(server.cb_body.ctx, res->generator, &next, &done, 0, 0);

        if(JS_IsException(ret)) {
          JSValue exception = JS_GetException(ctx);
          fprintf(stderr, "Exception: %s\n", JS_ToCString(ctx, exception));
          n = LWS_WRITE_HTTP_FINAL;
        } else if(!JS_IsUndefined(ret)) {
          if(1 || JS_IsString(ret)) {
            size_t len;
            int r;
            const char *x, *str = JS_ToCStringLen(server.cb_body.ctx, &len, ret);

            // lwsl_warn("len: %zu\n", len);

            for(x = str; len > 0; x += r, len -= r) {
              size_t l = len > 1024 ? 1024 : len;

              /*if(l == len)
                n = LWS_WRITE_HTTP_FINAL;*/

              r = lws_write(wsi, (uint8_t*)x, l, n);

              printf("lws_write(wsi, '%.*s', %zu) = %i (done = %i)\n", l, x, l, r, done);

              if(r <= 0)
                break;
            }

            if(r <= 0)
              return 1;

            JS_FreeCString(ctx, str);
          }
        }

        if(done)
          n = LWS_WRITE_HTTP_FINAL;

      } else {
        // n = LWS_WRITE_HTTP_FINAL;
        printf("WRITABLE unhandled\n");
        //   lws_write(wsi, "unhandled\n", strlen("unhandled\n"), n);
      }

      if(!done) {
        printf("lws_callback_on_writable %d\n", lws_get_socket_fd(wsi));
        lws_callback_on_writable(wsi);
      }

      /*
       * HTTP/1.0 no keepalive: close network connection
       * HTTP/1.1 or HTTP1.0 + KA: wait / process next transaction
       * HTTP/2: stream ended, parent connection remains up
       */
      if(n == LWS_WRITE_HTTP_FINAL) {
        int r;
        r = lws_write(wsi, "", 0, n);
        printf("lws_write() = %i\n", r);

        r = lws_http_transaction_completed(wsi);
        printf("LWS_WRITE_HTTP_FINAL r=%i\n", r);

        if(r)
          return -1;
      }
      break;
    }
    default: {
      minnet_lws_unhandled("HTTP", reason);
      break;
    }
  }
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

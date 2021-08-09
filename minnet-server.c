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

MinnetHttpServer minnet_server = {0};

static int callback_ws(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
static int callback_http(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

/**
 * @brief      Create HTTP server mount
 */
static MinnetHttpMount*
mount_create(JSContext* ctx, const char* mountpoint, const char* origin, const char* def, enum lws_mount_protocols origin_proto) {
  MinnetHttpMount* m = js_mallocz(ctx, sizeof(MinnetHttpMount));

  // printf("mount_create mnt=%-10s org=%-10s def=%s\n", mountpoint, origin, def);

  m->lws.mountpoint = js_strdup(ctx, mountpoint);
  m->lws.origin = origin ? js_strdup(ctx, origin) : 0;
  m->lws.def = def ? js_strdup(ctx, def) : 0;
  m->lws.protocol = "http";
  m->lws.origin_protocol = origin_proto;
  m->lws.mountpoint_len = strlen(mountpoint);

  return m;
}

static MinnetHttpMount*
mount_new(JSContext* ctx, JSValueConst obj) {
  MinnetHttpMount* ret;
  JSValue mnt = JS_UNDEFINED, org = JS_UNDEFINED, def = JS_UNDEFINED;

  if(JS_IsArray(ctx, obj)) {
    mnt = JS_GetPropertyUint32(ctx, obj, 0);
    org = JS_GetPropertyUint32(ctx, obj, 1);
    def = JS_GetPropertyUint32(ctx, obj, 2);
  } else if(JS_IsFunction(ctx, obj)) {
    size_t namelen;
    JSValue name = JS_GetPropertyStr(ctx, obj, "name");
    const char* namestr = JS_ToCStringLen(ctx, &namelen, name);
    char buf[namelen + 2];
    pstrcpy(&buf[1], namelen + 1, namestr);
    buf[0] = '/';
    buf[namelen + 1] = '\0';
    JS_FreeCString(ctx, namestr);
    mnt = JS_NewString(ctx, buf);
    org = JS_DupValue(ctx, obj);
    JS_FreeValue(ctx, name);
  }

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
    enum lws_mount_protocols proto = plen == 0 ? LWSMPRO_CALLBACK : !strncmp(dest, "https", plen) ? LWSMPRO_HTTPS : LWSMPRO_HTTP;

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

static struct http_mount*
mount_find(const char* x, size_t n) {
  struct lws_http_mount *ptr, *m = 0;
  int protocol = n == 0 ? LWSMPRO_CALLBACK : LWSMPRO_HTTP;
  size_t l = 0;
  if(n == 0)
    n = strlen(x);
  if(protocol == LWSMPRO_CALLBACK && x[0] == '/') {
    x++;
    n--;
  }
  int i = 0;
  for(ptr = (struct lws_http_mount*)minnet_server.info.mounts; ptr; ptr = (struct lws_http_mount*)ptr->mount_next) {
    if(protocol != LWSMPRO_CALLBACK || ptr->origin_protocol == LWSMPRO_CALLBACK) {
      const char* mnt = ptr->mountpoint;
      size_t len = ptr->mountpoint_len;
      if(protocol == LWSMPRO_CALLBACK && mnt[0] == '/') {
        mnt++;
        len--;
      }
      // printf("mount_find [%i] %.*s\n", i++, (int)len, mnt);
      if(len == n && !strncmp(x, mnt, n)) {
        m = ptr;
        l = n;
        break;
      }
      if(n >= len && len >= l && !strncmp(mnt, x, MIN(len, n))) {
        m = ptr;
        l = len;
      }
    }
  }
  return (struct http_mount*)m;
}

static void
mount_free(JSContext* ctx, MinnetHttpMount const* m) {
  js_free(ctx, (void*)m->lws.mountpoint);

  if(m->lws.origin)
    js_free(ctx, (void*)m->lws.origin);

  if(m->lws.def)
    js_free(ctx, (void*)m->lws.def);

  js_free(ctx, (void*)m);
}

static struct lws_protocols protocols[] = {
    {"minnet", callback_ws, sizeof(MinnetServerContext), 0, 0, 0, 1024},
    {"http", callback_http, sizeof(MinnetServerContext), 0, 0, 0, 1024},
    LWS_PROTOCOL_LIST_TERM,
};

// static const MinnetHttpMount* mount_dyn;

JSValue
minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  int a = 0;
  int port = 7981;
  memset(&minnet_server, 0, sizeof minnet_server);

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
    minnet_server.info.vhost_name = js_to_string(ctx, opt_host);
  else
    minnet_server.info.vhost_name = js_strdup(ctx, "localhost");

  GETCB(opt_on_pong, minnet_server.cb_pong)
  GETCB(opt_on_close, minnet_server.cb_close)
  GETCB(opt_on_connect, minnet_server.cb_connect)
  GETCB(opt_on_message, minnet_server.cb_message)
  GETCB(opt_on_fd, minnet_server.cb_fd)
  GETCB(opt_on_http, minnet_server.cb_http)

  protocols[0].user = ctx;
  protocols[1].user = ctx;

  minnet_server.ctx = ctx;
  minnet_server.info.port = port;
  minnet_server.info.protocols = protocols;
  minnet_server.info.mounts = 0;
  minnet_server.info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT /*| LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE*/;

  minnet_ws_sslcert(ctx, &minnet_server.info, options);

  if(JS_IsArray(ctx, opt_mounts)) {
    MinnetHttpMount** ptr = (MinnetHttpMount**)&minnet_server.info.mounts;
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

  if(!(minnet_server.context = lws_create_context(&minnet_server.info))) {
    lwsl_err("Libwebsockets init failed\n");
    return JS_EXCEPTION;
  }

  lws_service_adjust_timeout(minnet_server.context, 1, 0);

  while(a >= 0) {
    if(minnet_exception) {
      ret = JS_EXCEPTION;
      break;
    }

    if(minnet_server.cb_fd.ctx)
      js_std_loop(ctx);
    else
      a = lws_service(minnet_server.context, 20);
  }

  lws_context_destroy(minnet_server.context);

  if(minnet_server.info.mounts) {
    const MinnetHttpMount *mount, *next;

    for(mount = (MinnetHttpMount*)minnet_server.info.mounts; mount; mount = next) {
      next = (MinnetHttpMount*)mount->lws.mount_next;
      mount_free(ctx, mount);
    }
  }

  if(minnet_server.info.ssl_cert_filepath)
    JS_FreeCString(ctx, minnet_server.info.ssl_cert_filepath);

  if(minnet_server.info.ssl_private_key_filepath)
    JS_FreeCString(ctx, minnet_server.info.ssl_private_key_filepath);

  js_free(ctx, (void*)minnet_server.info.vhost_name);

  FREECB(minnet_server.cb_pong)
  FREECB(minnet_server.cb_close)
  FREECB(minnet_server.cb_connect)
  FREECB(minnet_server.cb_message)
  FREECB(minnet_server.cb_fd)
  FREECB(minnet_server.cb_http)

  return ret;
}

static int
callback_ws(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  JSValue ws_obj = JS_UNDEFINED;
  MinnetServerContext* serv = user;

  switch((int)reason) {
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
    case LWS_CALLBACK_PROTOCOL_INIT: return 0;

    case LWS_CALLBACK_ESTABLISHED: {
      // printf("%s fd=%d\n", lws_callback_name(reason), lws_get_socket_fd(wsi));

      if(minnet_server.cb_connect.ctx) {
        JSValue args[2];
        lws_set_opaque_user_data(wsi, 0);

        ws_obj = minnet_ws_wrap(minnet_server.cb_connect.ctx, wsi);
        args[0] = ws_obj;

        minnet_emit_this(&minnet_server.cb_connect, ws_obj, 1, args);

        if(serv)
          serv->ws_obj = ws_obj;
        else
          JS_FreeValue(minnet_server.cb_connect.ctx, args[0]);
      }
      return 0;
    }
      /*case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE: {
        uint8_t* codep = in;
        uint16_t code = (codep[0] << 8) + codep[1];
        const char* why = in + 2;
        int whylen = len - 2;

        printf("%s fd=%d code=%u len=%zu reason=%.*s\n", lws_callback_name(reason), lws_get_socket_fd(wsi), code, len, whylen, why);
        return 0;
      }*/

    // case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
    case LWS_CALLBACK_CLOSED: {
      JSValue why = JS_UNDEFINED;
      int code = -1;

      if(in) {
        uint8_t* codep = in;
        code = (codep[0] << 8) + codep[1];
        if(len - 2 > 0)
          why = JS_NewStringLen(minnet_server.ctx, in + 2, len - 2);
      }

      printf("%s fd=%d\n", lws_callback_name(reason), lws_get_socket_fd(wsi));

      if(minnet_server.cb_close.ctx) {
        JSValue cb_argv[3] = {JS_DupValue(minnet_server.cb_close.ctx, serv->ws_obj), code != -1 ? JS_NewInt32(minnet_server.cb_close.ctx, code) : JS_UNDEFINED, why};
        minnet_emit(&minnet_server.cb_close, code != -1 ? 3 : 1, cb_argv);
        JS_FreeValue(minnet_server.cb_close.ctx, cb_argv[0]);
        JS_FreeValue(minnet_server.cb_close.ctx, cb_argv[1]);
      }
      JS_FreeValue(minnet_server.ctx, why);
      JS_FreeValue(minnet_server.ctx, serv->ws_obj);
      serv->ws_obj = JS_NULL;
      return 0;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
      printf("%s fd=%d\n", lws_callback_name(reason), lws_get_socket_fd(wsi));
      lws_callback_on_writable(wsi);
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

static struct server_context*
get_context(void* user, struct lws* wsi) {
  MinnetServerContext* serv;

  if((serv = (MinnetServerContext*)user)) {

    if(!JS_IsObject(serv->ws_obj))
      serv->ws_obj = minnet_ws_object(minnet_server.ctx, wsi);
  }

  return serv;
}

static int
respond(struct lws* wsi, MinnetBuffer* buf, MinnetResponse* resp) {
  struct list_head* el;
  // printf("RESPOND\tstatus=%d type=%s\n", resp->status, resp->type);

  resp->read_only = TRUE;
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
   * at header-time makes life easier at the minnet_server.
   */
  if(lws_add_http_common_headers(wsi, resp->status, resp->type, LWS_ILLEGAL_HTTP_CONTENT_LEN, &buf->write, buf->end))
    return 1;

  list_for_each(el, &resp->headers) {
    struct http_header* hdr = list_entry(el, struct http_header, link);

    if((lws_add_http_header_by_name(wsi, (const unsigned char*)hdr->name, (const unsigned char*)hdr->value, strlen(hdr->value), &buf->write, buf->end)))
      JS_ThrowInternalError(minnet_server.cb_http.ctx, "lws_add_http_header_by_name failed");
  }

  if(lws_finalize_write_http_header(wsi, buf->start, &buf->write, buf->end))
    return 1;

  return 0;
}

static MinnetResponse*
request(MinnetCallback* cb, JSValue ws_obj, JSValue* args) {
  MinnetResponse* resp = minnet_response_data(minnet_server.ctx, args[1]);

  if(cb->ctx) {
    JSValue ret = minnet_emit_this(cb, ws_obj, 2, args);
    if(JS_IsObject(ret) && minnet_response_data(cb->ctx, ret)) {
      JS_FreeValue(cb->ctx, args[1]);
      args[1] = ret;
      resp = minnet_response_data(cb->ctx, ret);
      response_dump(resp);
    } else {
      JS_FreeValue(cb->ctx, ret);
    }
  }

  return resp;
}

static inline int
is_h2(struct lws* wsi) {
  return lws_get_network_wsi(wsi) != wsi;
}

static size_t
file_size(FILE* fp) {
  long pos = ftell(fp);
  size_t size = 0;

  if(fseek(fp, 0, SEEK_END) != -1) {
    size = ftell(fp);
    fseek(fp, pos, SEEK_SET);
  }
  return size;
}

static int
serve_file(struct lws* wsi, const char* path, struct http_mount* mount, struct http_response* resp, JSContext* ctx) {
  FILE* fp;
  // printf("\033[38;5;226mSERVE FILE\033[0m\tis_h2=%i path=%s mount=%s\n", is_h2(wsi), path, mount ? mount->mnt : 0);

  const char* mime = lws_get_mimetype(path, &mount->lws);

  if(path[0] == '\0')
    path = mount->def;

  if((fp = fopen(path, "rb"))) {
    size_t n = file_size(fp);

    buffer_alloc(&resp->body, n, ctx);

    if(fread(resp->body.write, n, 1, fp) == 1)
      resp->body.write += n;

    if(mime) {
      if(resp->type)
        js_free(ctx, resp->type);

      resp->type = js_strdup(ctx, mime);
    }

    fclose(fp);
  } else {
    const char* body = "<html><head><title>404 Not Found</title><meta charset=utf-8 http-equiv=\"Content-Language\" content=\"en\"/></head><body><h1>404 Not Found</h1></body></html>";
    resp->status = 404;
    resp->ok = FALSE;

    response_write(resp, body, strlen(body), ctx);
  }

  return 0;
}

static int
http_writable(struct lws* wsi, struct http_response* resp, BOOL done) {
  enum lws_write_protocol n = LWS_WRITE_HTTP;
  size_t r;

  if(done) {
    n = LWS_WRITE_HTTP_FINAL;
    /*  if(!buffer_REMAIN(&resp->body) && is_h2(wsi))
        buffer_append(&resp->body, "\nXXXXXXXXXXXXXX", 1, ctx);*/
  }

  if((r = buffer_REMAIN(&resp->body))) {
    uint8_t* x = resp->body.read;
    size_t l = is_h2(wsi) ? (r > 1024 ? 1024 : r) : r;

    if(l > 0) {
      if((r = lws_write(wsi, x, l, (r - l) > 0 ? LWS_WRITE_HTTP : n)) > 0)
        buffer_skip(&resp->body, r);
    }
  }

  if(done && buffer_REMAIN(&resp->body) == 0) {
    if(lws_http_transaction_completed(wsi))
      return -1;
  } else {
    /*
     * HTTP/1.0 no keepalive: close network connection
     * HTTP/1.1 or HTTP1.0 + KA: wait / process next transaction
     * HTTP/2: stream ended, parent connection remains up
     */
    lws_callback_on_writable(wsi);
  }

  return 0;
}

static int
callback_http(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  JSContext* ctx = minnet_server.ctx;
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE];
  MinnetHttpMethod method = METHOD_GET;
  MinnetServerContext* serv = user; // get_context(user, wsi);
  JSValue ws_obj = minnet_ws_object(ctx, wsi);
  //  MinnetWebsocket* ws = minnet_ws_data(ctx, ws_obj);
  char *url, *path, *mountpoint;
  size_t url_len, path_len, mountpoint_len;

  url = lws_uri_and_method(wsi, ctx, &method);
  url_len = url ? strlen(url) : 0;
  path = in;
  path_len = path ? strlen(path) : 0;

  if(url && path && path_len < url_len && !strcmp((url_len - path_len) + url, path)) {
    mountpoint_len = url_len - path_len;
    mountpoint = js_strndup(ctx, url, mountpoint_len);
  } else {
    mountpoint_len = 0;
    mountpoint = 0;
  }

  switch((int)reason) {
      /*   case (int)LWS_CALLBACK_CHECK_ACCESS_RIGHTS:
         case (int)LWS_CALLBACK_PROTOCOL_INIT:
         case (int)LWS_CALLBACK_HTTP_BIND_PROTOCOL:
         case (int)LWS_CALLBACK_HTTP_DROP_PROTOCOL:
         case (int)LWS_CALLBACK_CLOSED_HTTP:
         case (int)LWS_CALLBACK_HTTP_CONFIRM_UPGRADE:
         case (int)LWS_CALLBACK_FILTER_HTTP_CONNECTION: {
           break;
         }*/

    case LWS_CALLBACK_ADD_HEADERS: {
      /*struct lws_process_html_args* args = (struct lws_process_html_args*)in;
      printf("LWS_CALLBACK_ADD_HEADERS args: %.*s\n", args->len, args->p);*/
      break;
    }

    case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
      MinnetRequest* req = minnet_request_data(ctx, serv->req_obj);

      printf("LWS_CALLBACK_HTTP_BODY_COMPLETION\tis_h2=%i len: %zu, size: %zu, in: ", is_h2(wsi), len, buffer_OFFSET(&req->body));

      MinnetCallback* cb = /*minnet_server.cb_http.ctx ? &minnet_server.cb_http :*/ serv->mount ? &serv->mount->callback : 0;
      MinnetBuffer b = BUFFER(buf);
      MinnetResponse* resp = request(cb, ws_obj, serv->args);

      if(respond(wsi, &b, resp)) {
        JS_FreeValue(ctx, ws_obj);
        return 1;
      }

      if(cb && cb->ctx) {
        JSValue ret = minnet_emit_this(cb, ws_obj, 2, serv->args);

        assert(js_is_iterator(ctx, ret));
        serv->generator = ret;
      } else if(lws_http_transaction_completed(wsi)) {
        return -1;
      }

      lws_callback_on_writable(wsi);
      return 0;
    }

    case LWS_CALLBACK_HTTP_BODY: {
      MinnetRequest* req = minnet_request_data(ctx, serv->req_obj);

      printf("LWS_CALLBACK_HTTP_BODY\tis_h2=%i len: %zu, size: %zu, in: ", is_h2(wsi), len, buffer_OFFSET(&req->body));

      if(len) {
        buffer_append(&req->body, in, len, ctx);

        js_dump_string(in, len, 80);
        puts("");
      }
      return 0;
    }

    case LWS_CALLBACK_HTTP: {
      MinnetHttpMount* mount;
      MinnetCallback* cb = &minnet_server.cb_http;
      MinnetBuffer b = BUFFER(buf);
      JSValue* args = serv->args;
      char* path = in;
      int ret = 0;

      if(!(serv->mount = mount_find(mountpoint_len ? mountpoint : url, mountpoint_len ? mountpoint_len : 0)))
        serv->mount = mount_find(url, 0);

      if((mount = serv->mount)) {
        size_t mlen = strlen(mount->mnt);
        path = url;
        assert(!strncmp(url, mount->mnt, mlen));
        path += mlen;
      }
      if(!JS_IsObject(args[0]))
        args[0] = minnet_request_new(ctx, path, url, method);

      if(!JS_IsObject(args[1]))
        args[1] = minnet_response_new(ctx, url, 200, TRUE, "text/html");

      MinnetRequest* req = minnet_request_data(ctx, args[0]);
      MinnetResponse* resp = minnet_response_data(ctx, args[1]);

      ++req->ref_count;

      // printf("LWS_CALLBACK_HTTP\tis_h2=%i url=%s path=%s mountpoint=%s mount=%s\n", is_h2(wsi), url, path, mountpoint, mount ? mount->mnt : 0);

      if(mount && (mount->lws.origin_protocol == LWSMPRO_FILE || (mount->lws.origin_protocol == LWSMPRO_CALLBACK && mount->lws.origin))) {

        if((ret = serve_file(wsi, path, mount, resp, ctx)) == 0) {
          if(respond(wsi, &b, resp)) {
            JS_FreeValue(ctx, ws_obj);
            return 1;
          }
        }

      } else if(mount && mount->lws.origin_protocol == LWSMPRO_CALLBACK) {
        int tok, len;
        buffer_alloc(&req->header, 1024, ctx);
        for(tok = WSI_TOKEN_HOST; tok < WSI_TOKEN_COUNT; tok++) {
          if((len = lws_hdr_total_length(wsi, tok)) > 0) {
            char hdr[len + 1];
            lws_hdr_copy(wsi, hdr, len + 1, tok);
            hdr[len] = '\0';

            while(!buffer_printf(&req->header, "%s %s\n", lws_token_to_string(tok), hdr)) { buffer_grow(&req->header, 1024, ctx); }
          }
        }

        cb = &mount->callback;

        if(req->method == METHOD_GET || is_h2(wsi)) {
          resp = request(&minnet_server.cb_http, ws_obj, args);

          if(cb && cb->ctx) {
            JSValue ret = minnet_emit_this(cb, ws_obj, 2, args);
            assert(js_is_iterator(ctx, ret));
            serv->generator = ret;
          } else {

            printf("NO CALLBACK\turl=%s path=%s mountpoint=%s\n", url, path, mountpoint);
            if(lws_http_transaction_completed(wsi))
              return -1;
          }
          if(respond(wsi, &b, resp)) {
            JS_FreeValue(ctx, ws_obj);
            return 1;
          }
        }
      } else {
        printf("NOT FOUND\turl=%s path=%s mountpoint=%s\n", url, path, mountpoint);
        break;

        /* if(lws_add_http_common_headers(wsi, HTTP_STATUS_NOT_FOUND, "text/html", LWS_ILLEGAL_HTTP_CONTENT_LEN, &b.write, b.end))
           return 1;

         if(lws_finalize_write_http_header(wsi, b.start, &b.write, b.end))
           return 1;

         if(lws_http_transaction_completed(wsi))
           return 1;*/
      }

      if(req->method == METHOD_GET || is_h2(wsi))
        lws_callback_on_writable(wsi);

      JS_FreeValue(ctx, ws_obj);

      return ret;
    }

    case LWS_CALLBACK_HTTP_WRITEABLE: {

      MinnetResponse* resp = minnet_response_data(minnet_server.ctx, serv->resp_obj);
      MinnetRequest* req = minnet_request_data(minnet_server.ctx, serv->req_obj);
      BOOL done = FALSE;

      // printf("LWS_CALLBACK_HTTP_WRITEABLE[%zu]\tcb_http.ctx=%p url=%s path=%s mount=%s\n", serv->serial++, minnet_server.cb_http.ctx, url, req->path, serv->mount ? serv->mount->mnt : 0);

      if(JS_IsObject(serv->generator)) {
        JSValue ret, next = JS_UNDEFINED;

        ret = js_iterator_next(minnet_server.ctx, serv->generator, &next, &done, 0, 0);

        if(JS_IsException(ret)) {
          JSValue exception = JS_GetException(ctx);
          fprintf(stderr, "Exception: %s\n", JS_ToCString(ctx, exception));
          done = TRUE;
        } else if(!js_is_nullish(ret)) {
          JSBuffer buf = js_buffer_from(minnet_server.ctx, ret);
          buffer_append(&resp->body, buf.data, buf.size, ctx);
          js_buffer_free(&buf, minnet_server.ctx);
        }

      } else if(!buffer_OFFSET(&resp->body)) {
        printf("WRITABLE unhandled\n");
        // break;
      } else {
        done = TRUE;
      }

      return http_writable(wsi, resp, done);
    }

    case LWS_CALLBACK_HTTP_FILE_COMPLETION: {
      printf("\033[38;5;171mHTTP_FILE_COMPLETION\033[0m in = %s\n", url);
      break;
    }

    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE:
    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {
      // printf("\033[38;5;171m%s\033[0m in = %s, url = %s\n", lws_callback_name(reason) + 13, (char*)in, url);
      break;
    }
    case LWS_CALLBACK_PROTOCOL_INIT:
    case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_CHECK_ACCESS_RIGHTS:
    case LWS_CALLBACK_HTTP_DROP_PROTOCOL: {
      // printf("\033[38;5;171m%s\033[0m url = %s\n", lws_callback_name(reason) + 13, url);
      break;
    }
    case LWS_CALLBACK_CLOSED_HTTP: {
      if(serv) {
        JS_FreeValue(minnet_server.ctx, serv->req_obj);
        serv->req_obj = JS_UNDEFINED;
        JS_FreeValue(minnet_server.ctx, serv->resp_obj);
        serv->resp_obj = JS_UNDEFINED;
      }
      break;
    }
    default: {
      minnet_lws_unhandled(url, reason);
      break;
    }
  }

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

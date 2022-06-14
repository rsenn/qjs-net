#include <sys/types.h>
#include <cutils.h>
#include <ctype.h>
#include <libgen.h>
#include <assert.h>
#include <libwebsockets.h>

#include "jsutils.h"
#include "minnet-websocket.h"
#include "minnet-server.h"
#include "minnet-server-http.h"
#include "minnet-response.h"
#include "minnet-request.h"

MinnetVhostOptions*
vhost_options_create(JSContext* ctx, const char* name, const char* value) {
  MinnetVhostOptions* vo = js_mallocz(ctx, sizeof(MinnetVhostOptions));

#ifdef DEBUG_OUTPUT
  fprintf(stderr, "vhost_options_create %s %s\n", name, value);
#endif

  vo->name = name ? js_strdup(ctx, name) : 0;
  vo->value = value ? js_strdup(ctx, value) : 0;

  return vo;
}

MinnetVhostOptions*
vhost_options_new(JSContext* ctx, JSValueConst vhost_option) {
  MinnetVhostOptions* vo;
  JSValue name, value;
  const char *namestr, *valuestr;

  name = JS_GetPropertyUint32(ctx, vhost_option, 0);
  value = JS_GetPropertyUint32(ctx, vhost_option, 1);

  namestr = JS_ToCString(ctx, name);
  valuestr = JS_ToCString(ctx, value);

  JS_FreeValue(ctx, name);
  JS_FreeValue(ctx, value);

  vo = vhost_options_create(ctx, namestr, valuestr);

  JS_FreeCString(ctx, namestr);
  JS_FreeCString(ctx, valuestr);

  return vo;
}

void
vhost_options_free_list(JSContext* ctx, MinnetVhostOptions* vo) {
  MinnetVhostOptions* next;

  do {

    if(vo->name)
      js_free(ctx, (void*)vo->name);
    if(vo->value)
      js_free(ctx, (void*)vo->value);
    if(vo->options)
      vhost_options_free_list(ctx, vo->options);

    next = vo->next;
    js_free(ctx, (void*)vo);
  } while((vo = next));
}

void
vhost_options_free(JSContext* ctx, MinnetVhostOptions* vo) {

  if(vo->name)
    js_free(ctx, (void*)vo->name);
  if(vo->value)
    js_free(ctx, (void*)vo->value);

  if(vo->options)
    vhost_options_free_list(ctx, vo->options);

  js_free(ctx, (void*)vo);
}

MinnetHttpMount*
mount_create(JSContext* ctx, const char* mountpoint, const char* origin, const char* def, const char* pro, enum lws_mount_protocols origin_proto) {
  MinnetHttpMount* m;

  if((m = js_mallocz(ctx, sizeof(MinnetHttpMount)))) {
#ifdef DEBUG_OUTPUT

    printf("mount_create mnt=%-10s org=%-20s def=%-15s protocol=%-10s origin_protocol=%s\n",
           mountpoint,
           origin,
           def,
           pro,
           ((const char*[]){
               "HTTP",
               "HTTPS",
               "FILE",
               "CGI",
               "REDIR_HTTP",
               "REDIR_HTTPS",
               "CALLBACK",
           })[origin_proto]);
#endif
    m->lws.mountpoint = js_strdup(ctx, mountpoint);
    m->lws.origin = origin ? js_strdup(ctx, origin) : 0;
    m->lws.def = def ? js_strdup(ctx, def) : 0;
    m->lws.protocol = pro ? pro : js_strdup(ctx, /*origin_proto == LWSMPRO_CALLBACK ? "http" :*/ "defprot");
    m->lws.origin_protocol = origin_proto;
    m->lws.mountpoint_len = strlen(mountpoint);
  }

  return m;
}

MinnetHttpMount*
mount_new(JSContext* ctx, JSValueConst obj, const char* key) {
  MinnetHttpMount* ret;
  JSValue mnt = JS_UNDEFINED, org = JS_UNDEFINED, def = JS_UNDEFINED, pro = JS_UNDEFINED;

  if(JS_IsArray(ctx, obj)) {
    mnt = JS_GetPropertyUint32(ctx, obj, 0);
    org = JS_GetPropertyUint32(ctx, obj, 1);
    def = JS_GetPropertyUint32(ctx, obj, 2);
    pro = JS_GetPropertyUint32(ctx, obj, 3);
  } else if(JS_IsFunction(ctx, obj)) {

    if(!key) {
      size_t namelen;
      JSValue name = JS_GetPropertyStr(ctx, obj, "name");
      const char* namestr = JS_ToCStringLen(ctx, &namelen, name);
      char buf[namelen + 2];
      pstrcpy(&buf[1], namelen + 1, namestr);
      buf[0] = '/';
      buf[namelen + 1] = '\0';
      JS_FreeCString(ctx, namestr);
      mnt = JS_NewString(ctx, buf);
      JS_FreeValue(ctx, name);
    } else {
      mnt = JS_NewString(ctx, key);
    }

    org = JS_DupValue(ctx, obj);
  }

  const char* path = JS_ToCString(ctx, mnt);

#ifdef DEBUG_OUTPUT
  printf("mount_new '%s'\n", path);
#endif
  if(JS_IsFunction(ctx, org)) {
    ret = mount_create(ctx, path, 0, 0, 0, LWSMPRO_CALLBACK);

    GETCBTHIS(org, ret->callback, JS_UNDEFINED);

  } else {
    const char* dest = JS_ToCString(ctx, org);
    char* protocol = JS_IsString(pro) ? js_tostring(ctx, pro) : 0;
    const char* dotslashslash = strstr(dest, "://");
    size_t plen = dotslashslash ? dotslashslash - dest : 0;
    const char* origin = &dest[plen ? plen + 3 : 0];
    const char* index = JS_IsUndefined(def) ? 0 : JS_ToCString(ctx, def);
    enum lws_mount_protocols proto = plen == 0 ? LWSMPRO_CALLBACK : !strncmp(dest, "https", plen) ? LWSMPRO_HTTPS : LWSMPRO_HTTP;

    ret = mount_create(ctx, path, origin, index, protocol, proto);

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

struct http_mount*
mount_find(MinnetHttpMount* mounts, const char* x, size_t n) {
  struct lws_http_mount *p, *m = 0;
  int i = 0, protocol = n == 0 ? LWSMPRO_CALLBACK : LWSMPRO_HTTP;
  size_t l = 0;
  if(n == 0)
    n = strlen(x);
  if(protocol == LWSMPRO_CALLBACK && x[0] == '/') {
    x++;
    n--;
  }
  for(p = (struct lws_http_mount*)mounts; p; p = (struct lws_http_mount*)p->mount_next) {
    if(protocol != LWSMPRO_CALLBACK || p->origin_protocol == LWSMPRO_CALLBACK) {
      const char* mnt = p->mountpoint;
      size_t len = p->mountpoint_len;
      if(protocol == LWSMPRO_CALLBACK && mnt[0] == '/') {
        mnt++;
        len--;
      }
      // printf("mount_find i=%d x='%.*s' '%.*s'\n", i++, (int)n, x, (int)len, mnt);

      if((len == n || (n > len && (x[len] == '/' || x[len] == '?'))) && !strncmp(x, mnt, n)) {
        m = p;
        /*   break;*/
      }
      if(n >= len && len >= l && !strncmp(mnt, x, MIN(len, n))) {
        m = p;
      }
    }
  }
#ifdef DEBUG_OUTPUT
  if(m)
    printf("mount_find org=%s mnt=%s cb.ctx=%p\n", ((struct http_mount*)m)->org, ((struct http_mount*)m)->mnt, ((struct http_mount*)m)->callback.ctx);
#endif
  return (struct http_mount*)m;
}

struct http_mount*
mount_find_s(MinnetHttpMount* mounts, const char* x) {
  struct lws_http_mount *p, *m = 0;
  size_t n = strlen(x);

  for(p = (struct lws_http_mount*)mounts; p; p = (struct lws_http_mount*)p->mount_next) {
    const char* mnt = p->mountpoint;
    size_t len = p->mountpoint_len;

    // printf("mount x='%.*s' '%.*s'\n", (int)n, x, (int)len, mnt);
    if(len == n && !strncmp(x, mnt, n)) {
      m = p;
      break;
    }
    if(len == 1 && mnt[0] == '/') {
      m = p;
    }
    if((n > len && (x[len] == '/' || x[len] == '?')) && (len == 0 || !strncmp(x, mnt, len))) {
      m = p;
      break;
    }
  }
  return (struct http_mount*)m;
}

void
mount_free(JSContext* ctx, MinnetHttpMount const* m) {
  js_free(ctx, (void*)m->lws.mountpoint);

  if(m->org)
    js_free(ctx, (void*)m->org);

  if(m->def)
    js_free(ctx, (void*)m->def);

  if(m->pro)
    js_free(ctx, (void*)m->pro);

  js_free(ctx, (void*)m);
}

int
http_server_respond(struct lws* wsi, MinnetBuffer* buf, struct http_response* resp, JSContext* ctx) {
  struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);
  int is_ssl = lws_is_ssl(wsi);
  int is_h2 = lws_wsi_is_h2(wsi);

  LOG("SERVER-HTTP",
      FG("%d") "%-38s" NC " wsi#%" PRId64 " status=%d type=%s length=%zu",
      165,
      "http-server-respond",
      opaque->serial,
      resp->status,
      resp->type,
      resp->body ? buffer_HEAD(resp->body) : 0);

  // resp->read_only = TRUE;
  response_generator(resp, ctx);

  if(lws_add_http_common_headers(wsi, resp->status, resp->type, is_ssl || is_h2 ? LWS_ILLEGAL_HTTP_CONTENT_LEN : buffer_HEAD(resp->body), &buf->write, buf->end)) {
    return 1;
  }
  /*  {
      char* b = buffer_escaped(buf, ctx);

      lwsl_user("lws_add_http_common_headers %td '%s'", buf->write - buf->start, b);
      js_free(ctx, b);
    }*/

  {
    size_t len, n;
    uint8_t *x, *end;
    for(x = resp->headers.start, end = resp->headers.write; x < end; x += len + 1) {
      len = byte_chrs(x, end - x, "\r\n", 2);
      if(len > (n = byte_chr(x, len, ':'))) {
        const char* prop = js_strndup(ctx, (const char*)x, n);
        if(x[n] == ':')
          n++;
        if(isspace(x[n]))
          n++;

#ifdef DEBUG_OUTPUT
        printf("HTTP header %s = %.*s\n", prop, (int)(len - n), &x[n]);
#endif
        if((lws_add_http_header_by_name(wsi, (const unsigned char*)prop, (const unsigned char*)&x[n], len - n, &buf->write, buf->end)))
          JS_ThrowInternalError(ctx, "lws_add_http_header_by_name failed");
        js_free(ctx, (void*)prop);
      }
    }
  }
  int ret = lws_finalize_write_http_header(wsi, buf->start, &buf->write, buf->end);

#ifdef DEBUG_OUTPUT

  printf("HTTP headers '%.*s'\n", (int)buffer_HEAD(buf), buf->start);
#endif
  /* {
     char* b = buffer_escaped(buf, ctx);
     lwsl_user("lws_finalize_write_http_header '%s' %td ret=%d", b, buf->write - buf->start, ret);
      js_free(ctx, b);
   }*/
  if(ret)
    return 2;

  return 0;
}

static MinnetResponse*
request_handler(MinnetSession* session, MinnetCallback* cb) {
  MinnetResponse* resp = minnet_response_data2(cb->ctx, session->resp_obj);

  if(cb && cb->ctx) {
    JSValue ret = minnet_emit_this(cb, session->ws_obj, 2, session->args);
    lwsl_user("request_handler ret=%s", JS_ToCString(cb->ctx, ret));
    if(JS_IsObject(ret) && minnet_response_data2(cb->ctx, ret)) {
      JS_FreeValue(cb->ctx, session->args[1]);
      session->args[1] = ret;
      resp = minnet_response_data2(cb->ctx, ret);
    } else {
      JS_FreeValue(cb->ctx, ret);
    }
  }
  lwsl_user("request_handler %s", response_dump(resp));

  return resp;
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
  const char* mime = lws_get_mimetype(path, &mount->lws);

#ifdef DEBUG_OUTPUT
  printf("serve_file path=%s mount=%s\n", path, mount->mnt);
#endif

  if(path[0] == '\0') {
    path = mount->def;
    /* printf("serve_file def=%s\n", path, mount->def);
     response_redirect(resp, mount->def, ctx);
     return 0;*/
  }

  /*{
    char disposition[1024];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", basename(path));
    headers_set(ctx, &resp->headers, "Content-Disposition", disposition);
  }*/
  response_generator(resp, ctx);

  if((fp = fopen(path, "rb"))) {
    size_t n = file_size(fp);

    buffer_alloc(resp->body, n, ctx);

    if(fread(resp->body->write, n, 1, fp) == 1)
      resp->body->write += n;

    if(mime) {
      if(resp->type)
        js_free(ctx, resp->type);

      resp->type = js_strdup(ctx, mime);
    }

    fclose(fp);
  } else {
    const char* body = "<html>\n  <head>\n    <title>404 Not Found</title>\n    <meta charset=utf-8 http-equiv=\"Content-Language\" content=\"en\"/>\n  </head>\n  <body>\n    <h1>404 Not "
                       "Found</h1>\n  </body>\n</html>\n";
    resp->status = 404;
    resp->ok = FALSE;

    response_write(resp, body, strlen(body), ctx);
  }

  lwsl_user("serve_file path=%s mount=%.*s length=%td", path, mount->lws.mountpoint_len, mount->lws.mountpoint, buffer_HEAD(resp->body));

  return 0;
}

int
http_server_writable(struct lws* wsi, struct http_response* resp, BOOL done) {
  struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);
  enum lws_write_protocol n, p = -1;
  size_t remain;
  ssize_t ret = 0;

  LOG("SERVER-HTTP", FG("%d") "%-38s" NC " wsi#%" PRId64 " status=%d type=%s length=%zu", 112, __func__ + 12, opaque->serial, resp->status, resp->type, resp->body ? buffer_HEAD(resp->body) : 0);

  n = done ? LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP;
  /*  if(!buffer_BYTES(resp->body) && is_h2(wsi)) buffer_append(resp->body, "\nXXXXXXXXXXXXXX", 1, ctx);*/

  if((remain = buffer_REMAIN(resp->body))) {
    uint8_t* x = resp->body->read;
    size_t l = is_h2(wsi) ? (remain > 1024 ? 1024 : remain) : remain;

    if(l > 0) {
      p = (remain - l) > 0 ? LWS_WRITE_HTTP : n;
      ret = lws_write(wsi, x, l, p);
      LOG("SERVER-HTTP", FG("%d") "%-38s" NC " wsi#%" PRIi64 " len=%zu final=%d ret=%zd", 112, __func__ + 12, opaque->serial, l, p == LWS_WRITE_HTTP_FINAL, ret);

      buffer_skip(resp->body, ret);
    }
  }

  remain = buffer_BYTES(resp->body);

  LOG("SERVER-HTTP", FG("%d") "%-38s" NC " wsi#%" PRIi64 " done=%i remain=%zu final=%d", 112, __func__ + 12, opaque->serial, done, remain, p == LWS_WRITE_HTTP_FINAL);

  if(p == LWS_WRITE_HTTP_FINAL || (done && remain == 0)) {

    if(lws_http_transaction_completed(wsi))
      return 1;

    return 0;
  }

  if(remain > 0)
    lws_callback_on_writable(wsi);

  return 0;
}

int
http_server_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  int ret = 0;
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE];
  MinnetServer* server = lws_context_user(lws_get_context(wsi));
  MinnetSession* session = user;
  JSContext* ctx = server ? server->context.js : 0;
  struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);

  if(lws_is_poll_callback(reason)) {
    assert(server);
    return fd_callback(wsi, reason, &server->cb.fd, in);
  }

  if(reason == LWS_CALLBACK_HTTP_CONFIRM_UPGRADE) {
    if(session && session->serial != opaque->serial) {
      session->serial = opaque->serial;
      session->h2 = is_h2(wsi);
    }
  }

  if(!opaque && ctx)
    opaque = lws_opaque(wsi, ctx);

  assert(opaque);

  LOGCB("HTTP",
        "%s%sfd=%d in='%.*s' url=%s session#%d",
        is_h2(wsi) ? "h2, " : "",
        lws_is_ssl(wsi) ? "ssl, " : "",
        lws_get_socket_fd(lws_get_network_wsi(wsi)),
        (int)len,
        in,
        opaque && opaque->req ? url_string(&opaque->req->url) : 0,
        session ? session->serial : 0);

  switch(reason) {
    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: break;

    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CHECK_ACCESS_RIGHTS: {
      return 0;
      break;
    }
    case LWS_CALLBACK_PROTOCOL_INIT: {
      // session_zero(session);
      return 0;
    }
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS:
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
    case LWS_CALLBACK_PROTOCOL_DESTROY: {

      break;
    }

    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: {
      JSValueConst args[2] = {session->ws_obj, JS_NULL};

      if(!lws_is_ssl(wsi) && !strcmp(in, "h2c"))
        return -1;

      if(!opaque->req) {
        /*char* uri;
        MinnetHttpMethod method;

        uri = minnet_uri_and_method(ctx, wsi, &method);
        if(uri)*/
        opaque->req = request_fromwsi(wsi, ctx);
      }

      int num_hdr = headers_get(ctx, &opaque->req->headers, wsi);

      LOGCB("HTTP", "fd=%i, num_hdr=%i", lws_get_socket_fd(lws_get_network_wsi(wsi)), num_hdr);

      /*return 1;*/
      return 0;
    }

    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {
      break;
    }
    case LWS_CALLBACK_HTTP_BIND_PROTOCOL: {
      if(!opaque->req)
        opaque->req = request_fromwsi(wsi, ctx);

      opaque->status = OPEN;

      url_set_protocol(&opaque->req->url, lws_is_ssl(wsi) ? "https" : "http");

      // LOGCB("HTTP", "url=%s", opaque->req ? url_string(&opaque->req->url) : 0);
      return 0;
    }

    case LWS_CALLBACK_ADD_HEADERS: {
      break;
    }

    case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
      MinnetCallback* cb = session->mount ? &session->mount->callback : 0;
      MinnetBuffer b = BUFFER(buf);
      MinnetResponse* resp = request_handler(session, cb);

      if(cb && cb->ctx) {
        JSValue ret = minnet_emit_this(cb, session->ws_obj, 2, session->args);

        assert(js_is_iterator(ctx, ret));
        session->generator = ret;
      } /* else if(lws_http_transaction_completed(wsi)) {
         return -1;
       }
  */
      lws_callback_on_writable(wsi);
      return 0;
    }

    case LWS_CALLBACK_HTTP_BODY: {
      MinnetRequest* req = minnet_request_data2(ctx, session->req_obj);

      LOGCB("HTTP", "%slen: %zu, size: %zu", is_h2(wsi) ? "h2, " : "", len, buffer_HEAD(&req->body));

      if(len) {
        buffer_append(&req->body, in, len, ctx);

        js_dump_string(in, len, 80);
        puts("");
      }
      return 0;
    }

    case LWS_CALLBACK_HTTP: {
      MinnetURL* url = &opaque->req->url;
      MinnetHttpMount* mount;
      MinnetBuffer b = BUFFER(buf);
      JSValue* args = session->args;
      char* path = in;
      size_t mountpoint_len = 0, pathlen = strlen(url->path);

      if(url->path && in && len < pathlen /*&& !strcmp(url->path, in)*/)
        mountpoint_len = pathlen - len;

      LOGCB("HTTP", "mountpoint='%.*s' path='%s'", (int)mountpoint_len, url->path, path);

      if(!opaque->req->headers.write) {
        int num_hdr = headers_get(ctx, &opaque->req->headers, wsi);
      }
      /*
            if(!opaque->req->path[0])
              pstrcpy(opaque->req->path, sizeof(opaque->req->path), path);
      */
      if(!session->mount)
        session->mount = mount_find((MinnetHttpMount*)server->context.info.mounts, url->path, mountpoint_len);
      if(!session->mount)
        session->mount = mount_find((MinnetHttpMount*)server->context.info.mounts, path, 0);
      if(url->path && !session->mount)
        if(!(session->mount = mount_find((MinnetHttpMount*)server->context.info.mounts, url->path, mountpoint_len)))
          session->mount = ((MinnetHttpMount*)server->context.info.mounts, url->path, 0);

      session->h2 = is_h2(wsi);

      if((mount = session->mount)) {
        size_t mlen = strlen(mount->mnt);
        assert(!strncmp(url->path, mount->mnt, mlen));

        if(!strcmp(url->path + mlen, path)) {
          assert(!strcmp(url->path + mlen, path));

          LOGCB("HTTP",
                "mount: mnt='%s', org='%s', pro='%s', origin_protocol='%s'\n",
                mount->mnt,
                mount->org,
                mount->pro,
                ((const char*[]){"HTTP", "HTTPS", "FILE", "CGI", "REDIR_HTTP", "REDIR_HTTPS", "CALLBACK"})[(uintptr_t)mount->lws.origin_protocol]);
        }
      }

      args[0] = session->req_obj = minnet_request_wrap(ctx, opaque->req);

      if(!JS_IsObject(args[1]))
        args[1] = minnet_response_new(ctx, *url, /*opaque->req->method == METHOD_POST ? 201 :*/ 200, 0, TRUE, "text/html");

      MinnetRequest* req = opaque->req;
      MinnetResponse* resp = opaque->resp = minnet_response_data2(ctx, args[1]);

      LOGCB("HTTP", "req=%p, header=%zu", req, buffer_HEAD(&req->headers));

      request_dup(req);

      MinnetCallback* cb = &mount->callback;

      if(mount && !mount->callback.ctx)
        cb = 0;

      if(mount && ((mount->lws.origin_protocol == LWSMPRO_CALLBACK && (!cb || !cb->ctx)) ||
                   (mount->lws.origin_protocol == LWSMPRO_FILE || (mount->lws.origin_protocol == LWSMPRO_CALLBACK && mount->lws.origin)))) {

        ret = serve_file(wsi, path, mount, resp, ctx);

        if(ret) {
          LOGCB("HTTP", "serve_file FAIL %d", ret);
          JS_FreeValue(ctx, session->ws_obj);
          session->ws_obj = JS_NULL;
          lws_callback_on_writable(wsi);
          return 0;
        }
        /* ret = http_server_respond(wsi, &b, resp, ctx);
         if(ret) {
           LOGCB("HTTP", "http_server_respond FAIL %d", ret);
           JS_FreeValue(ctx, session->ws_obj);
           session->ws_obj = JS_NULL;
           lws_callback_on_writable(wsi);
           return 0;
         }

        // if(ret == 0)
           lws_callback_on_writable(wsi);
           return 0;*/
      }

      if(mount && mount->lws.origin_protocol == LWSMPRO_CALLBACK) {

        if(cb && cb->ctx) {
          if(req->method == METHOD_GET /* || is_h2(wsi)*/) {
            resp = request_handler(session, cb);

            JSValue gen = minnet_emit_this(cb, session->ws_obj, 2, args);
            if(js_is_iterator(ctx, gen)) {
              assert(js_is_iterator(ctx, gen));
              LOGCB("HTTP", "gen=%s", JS_ToCString(ctx, gen));

              session->generator = gen;
              session->next = JS_UNDEFINED;
              /* lws_callback_on_writable(wsi);
               return 0;*/
            } else {
              LOGCB("HTTP", "gen=%s", JS_ToCString(ctx, gen));
            }
          }
        }

        /*     LOGCB("HTTP", "path=%s mountpoint=%.*s", path, (int)mountpoint_len, url->path);
           if(lws_http_transaction_completed(wsi))
              return -1;
          }

        }*/
      }

      if(server->cb.http.ctx) {
        cb = &server->cb.http;
        JSValue val = minnet_emit_this(cb, session->ws_obj, 2, args);
      }

      if((ret = http_server_respond(wsi, &b, resp, ctx))) {
        JS_FreeValue(ctx, session->ws_obj);
        session->ws_obj = JS_NULL;
        return 1;
      }

      goto http_exit;

      /*      LOGCB("HTTP", "NOT FOUND\tpath=%s mountpoint=%.*s", path, (int)mountpoint_len, url->path);
            if(cb && cb->ctx)
              server_exception(server, minnet_emit(cb, 2, &session->req_obj));*/

    http_exit:
      if(req->method == METHOD_GET || is_h2(wsi))
        lws_callback_on_writable(wsi);

      JS_FreeValue(ctx, session->ws_obj);

      return 0;
    }

    case LWS_CALLBACK_HTTP_WRITEABLE: {
      MinnetBuffer b = BUFFER(buf);
      MinnetResponse* resp; // = minnet_response_data2(ctx, session->resp_obj);
      BOOL done = FALSE;

      if(!(resp = opaque->resp)) {
        resp = opaque->resp = response_new(ctx);
        session->resp_obj = minnet_response_wrap(ctx, resp);
      }

      LOGCB("HTTP",
            "%smnt=%s remain=%td type=%s url.path=%s",
            session->h2 ? "h2, " : "",
            session->mount ? session->mount->mnt : 0,
            resp && resp->body ? buffer_BYTES(resp->body) : 0,
            resp ? resp->type : 0,
            resp ? resp->url.path : 0);

      if(JS_IsObject(session->generator)) {
        JSValue ret = JS_UNDEFINED;
        JSBuffer out = JS_BUFFER(0, 0, 0);

        session->next = JS_UNDEFINED;

        // printf("LWS_CALLBACK_HTTP_WRITEABLE: %s\n", JS_ToCString(ctx, session->generator));

        while(!done) {
          ret = js_iterator_next(ctx, session->generator, &session->next, &done, 0, 0);

          if(JS_IsException(ret)) {
            JSValue exception = JS_GetException(ctx);
            js_error_print(ctx, exception);
            done = TRUE;
          } else if(!done) {
            out = js_buffer_new(ctx, ret);
            LOGCB("HTTP-WRITEABLE", "size=%zu, out='%.*s'", out.size, (int)(out.size > 255 ? 255 : out.size), out.size > 255 ? &out.data[out.size - 255] : out.data);
#ifdef DEBUG_OUTPUT
            printf("\x1b[2K\ryielded %.*s %zu\n", (int)(out.size > 255 ? 255 : out.size), out.size > 255 ? &out.data[out.size - 255] : out.data, out.size);
#endif
            if(!resp->generator)
              response_generator(resp, ctx);

            buffer_append(resp->body, out.data, out.size, ctx);
            js_buffer_free(&out, ctx);
            break;
          }
        }

      } else {
        done = TRUE;
      }
      LOGCB("HTTP-WRITEABLE", "status=%s done=%i write=%zu", ((const char*[]){"CONNECTING", "OPEN", "CLOSING", "CLOSED"})[opaque->status], done, resp->body ? buffer_HEAD(resp->body) : 0);
      if(!resp->body || !buffer_HEAD(resp->body)) {
        static int unhandled;

        if(!unhandled++)
          LOGCB("HTTP", "unhandled %d", unhandled);

        break;
      }

      /* if(opaque->status == OPEN) {
         if(http_server_respond(wsi, &b, resp, ctx)) {
           JS_FreeValue(ctx, session->ws_obj);
           session->ws_obj = JS_NULL;

           return 1;
         }
         opaque->status = CLOSING;
       }
  */
      if(http_server_writable(wsi, resp, done) == 1)
        return http_server_callback(wsi, LWS_CALLBACK_HTTP_FILE_COMPLETION, user, in, len);

      return 0;
    }

    case LWS_CALLBACK_HTTP_DROP_PROTOCOL: {
      return 0;
    }

    case LWS_CALLBACK_HTTP_FILE_COMPLETION: {

      if(opaque->req) {
        request_free(opaque->req, ctx);
        opaque->req = 0;
      }

      if(opaque->resp) {
        response_free(opaque->resp, ctx);
        opaque->resp = 0;
      }

      return lws_callback_http_dummy(wsi, reason, user, in, len);
      break;
    }

    case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
    case LWS_CALLBACK_CLOSED_HTTP: {
      LOGCB("HTTP", "in='%.*s' url=%s", (int)len, (char*)in, opaque->req ? url_string(&opaque->req->url) : 0);
      // lws_close_free_wsi(wsi, LWS_CLOSE_STATUS_NOSTATUS, __func__);
      /*     if(session) {
             JS_FreeValue(ctx, session->req_obj);
             session->req_obj = JS_UNDEFINED;
             JS_FreeValue(ctx, session->resp_obj);
             session->resp_obj = JS_UNDEFINED;
           }*/
      return -1;
    }
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
    case LWS_CALLBACK_GET_THREAD_ID: {
      return 0;
    }
    default: {
      minnet_lws_unhandled(__func__, reason);
      break;
    }
  }
  // int ret = 0;
  if(reason != LWS_CALLBACK_HTTP_WRITEABLE && (reason < LWS_CALLBACK_HTTP_BIND_PROTOCOL || reason > LWS_CALLBACK_CHECK_ACCESS_RIGHTS)) {
    LOGCB("HTTP", "fd=%i %s%sin='%.*s' ret=%d\n", lws_get_socket_fd(wsi), (session && session->h2) || is_h2(wsi) ? "h2, " : "", lws_is_ssl(wsi) ? "ssl, " : "", (int)len, (char*)in, ret);
  }

  if(ret == 0)
    ret = lws_callback_http_dummy(wsi, reason, user, in, len);

  return ret;
}

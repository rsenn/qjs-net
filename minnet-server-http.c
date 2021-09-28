#include <sys/types.h>
#include <cutils.h>
#include <ctype.h>

#include "jsutils.h"
#include "minnet-websocket.h"
#include "minnet-server.h"
#include "minnet-server-http.h"
#include "minnet-response.h"
#include "minnet-request.h"

MinnetHttpMount*
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

MinnetHttpMount*
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

struct http_mount*
mount_find(const char* x, size_t n) {
  struct lws_http_mount *p, *m = 0;
  int protocol = n == 0 ? LWSMPRO_CALLBACK : LWSMPRO_HTTP;
  size_t l = 0;
  if(n == 0)
    n = strlen(x);
  if(protocol == LWSMPRO_CALLBACK && x[0] == '/') {
    x++;
    n--;
  }
  for(p = (struct lws_http_mount*)minnet_server.info.mounts; p; p = (struct lws_http_mount*)p->mount_next) {
    if(protocol != LWSMPRO_CALLBACK || p->origin_protocol == LWSMPRO_CALLBACK) {
      const char* mnt = p->mountpoint;
      size_t len = p->mountpoint_len;
      if(protocol == LWSMPRO_CALLBACK && mnt[0] == '/') {
        mnt++;
        len--;
      }
      if(len == n && !strncmp(x, mnt, n)) {
        m = p;
        break;
      }
      if(n >= len && len >= l && !strncmp(mnt, x, MIN(len, n))) {
        m = p;
      }
    }
  }
  return (struct http_mount*)m;
}

void
mount_free(JSContext* ctx, MinnetHttpMount const* m) {
  js_free(ctx, (void*)m->lws.mountpoint);

  if(m->lws.origin)
    js_free(ctx, (void*)m->lws.origin);

  if(m->lws.def)
    js_free(ctx, (void*)m->lws.def);

  js_free(ctx, (void*)m);
}

int
http_respond(struct lws* wsi, MinnetBuffer* buf, MinnetResponse* resp, JSContext* ctx) {

  struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);
  int is_ssl = lws_is_ssl(wsi);

  lwsl_user("http " FG("198") "%-25s" NC " wsi#%i url=%s status=%d type=%s length=%zu", "RESPOND", opaque->serial, resp->url, resp->status, resp->type, buffer_WRITE(&resp->body));

  resp->read_only = TRUE;

  if(lws_add_http_common_headers(wsi, resp->status, resp->type, is_ssl ? LWS_ILLEGAL_HTTP_CONTENT_LEN : buffer_WRITE(&resp->body), &buf->write, buf->end)) {
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
      len = byte_chr(x, end - x, '\n');
      if(len > (n = byte_chr(x, len, ':'))) {
        const char* prop = js_strndup(ctx, (const char*)x, n);
        if(x[n] == ':')
          n++;
        if(isspace(x[n]))
          n++;
        if((lws_add_http_header_by_name(wsi, (const unsigned char*)prop, (const unsigned char*)&x[n], len - n, &buf->write, buf->end)))
          JS_ThrowInternalError(minnet_server.cb_http.ctx, "lws_add_http_header_by_name failed");
        js_free(ctx, (void*)prop);
      }
    }
  }
  int ret = lws_finalize_write_http_header(wsi, buf->start, &buf->write, buf->end);

  /* {
     char* b = buffer_escaped(buf, ctx);
     lwsl_debug("lws_finalize_write_http_header '%s' %td ret=%d", b, buf->write - buf->start, ret);
      js_free(ctx, b);
   }*/
  if(ret)
    return 2;

  return 0;
}

static MinnetResponse*
request_handler(MinnetSession* serv, MinnetCallback* cb) {
  MinnetResponse* resp = minnet_response_data2(minnet_server.ctx, serv->resp_obj);

  if(cb && cb->ctx) {
    JSValue ret = minnet_emit_this(cb, serv->ws_obj, 2, serv->args);
    lwsl_user("request_handler ret=%s", JS_ToCString(cb->ctx, ret));
    if(JS_IsObject(ret) && minnet_response_data2(cb->ctx, ret)) {
      JS_FreeValue(cb->ctx, serv->args[1]);
      serv->args[1] = ret;
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

  lwsl_debug("serve_file path=%s mount=%.*s length=%u", path, mount->lws.mountpoint_len, mount->lws.mountpoint, buffer_WRITE(&resp->body));

  return 0;
}

int
http_writable(struct lws* wsi, struct http_response* resp, BOOL done) {
  enum lws_write_protocol n = LWS_WRITE_HTTP;
  size_t remain;
  ssize_t ret = 0;

  if(done) {
    n = LWS_WRITE_HTTP_FINAL;
    /*  if(!buffer_REMAIN(&resp->body) && is_h2(wsi)) buffer_append(&resp->body, "\nXXXXXXXXXXXXXX", 1, ctx);*/
  }

  if((remain = buffer_REMAIN(&resp->body))) {
    uint8_t* x = resp->body.read;
    size_t l = is_h2(wsi) ? (remain > 1024 ? 1024 : remain) : remain;

    if(l > 0) {
      int p = (remain - l) > 0 ? LWS_WRITE_HTTP : n;
      lwsl_debug("lws_write len=%zu final=%d", l, p == LWS_WRITE_HTTP_FINAL);
      if((ret = lws_write(wsi, x, l, p)) > 0)
        buffer_skip(&resp->body, ret);
    }
  }

  lwsl_user("http_writable done=%i remain=%zu final=%d", done, buffer_REMAIN(&resp->body), n == LWS_WRITE_HTTP_FINAL);

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

int
http_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  JSContext* ctx = minnet_server.ctx;
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE];
  MinnetHttpMethod method = -1;
  MinnetSession* serv = user;
  JSValue ws_obj = minnet_ws_object(ctx, wsi);
  struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);
  char* url;
  size_t url_len;

  if(serv) {
    if(reason == LWS_CALLBACK_FILTER_HTTP_CONNECTION || reason == LWS_CALLBACK_HTTP_CONFIRM_UPGRADE) {
      serv->serial = opaque->serial;
      serv->h2 = is_h2(wsi);
    }
  }

  if(opaque->req) {
    url = opaque->req->url;
    method = opaque->req->method;
  } else {
    url = lws_uri_and_method(wsi, ctx, &method);
  }
  url_len = url ? strlen(url) : 0;

  /*if(url || (in && *(char*)in) || (path && *path)) */

  if(reason != LWS_CALLBACK_HTTP_WRITEABLE && (reason < LWS_CALLBACK_HTTP_BIND_PROTOCOL || reason > LWS_CALLBACK_CHECK_ACCESS_RIGHTS)) {
    lwsl_user("http " FG("%d") "%-25s" NC " wsi#%i fd=%i is_h2=%i is_ssl=%i url=%s method=%s in='%.*s'\n",
              22 + (reason * 2),
              lws_callback_name(reason) + 13,
              opaque->serial,
              lws_get_socket_fd(wsi),
              serv && serv->h2 || is_h2(wsi),
              lws_is_ssl(wsi),
              url,
              method_name(method),
              (int)len,
              (char*)in);
  }

  switch((int)reason) {
    case(int)LWS_CALLBACK_ESTABLISHED: {
      case(int)LWS_CALLBACK_CHECK_ACCESS_RIGHTS:
      case(int)LWS_CALLBACK_PROTOCOL_INIT: break;
    }

    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: {
      JSValueConst args[2] = {ws_obj, JS_NULL};

      if(!opaque->req)
        opaque->req = request_new(minnet_server.ctx, in, url, method);

      int num_hdr = http_headers(ctx, &opaque->req->headers, wsi);

      lwsl_user("http " FGC(171, "%-25s") " fd=%i, num_hdr=%i\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), num_hdr);

      /*   args[1] = minnet_request_wrap(minnet_server.ctx, opaque->req);
         minnet_emit_this(&minnet_server.cb_connect, ws_obj, 2, args);
         JS_FreeValue(ctx, args[1]);*/

      return 1;
      break;
    }

    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {

      if(!opaque->req)
        opaque->req = request_new(ctx, 0, url, method);

      MinnetBuffer* h = &opaque->req->headers;
      int num_hdr = http_headers(ctx, h, wsi);
      lwsl_user("http " FGC(171, "%-25s") " %s\n", lws_callback_name(reason) + 13, request_dump(opaque->req, ctx));

      /*char* header_data = buffer_escaped(buf, ctx);
      lwsl_user("http " FGC(171, "%-25s") " headers: " FGC(214, "%s") "\n", lws_callback_name(reason) + 13, header_data);
      js_free(ctx, header_data);*/
      break;
    }

    case LWS_CALLBACK_ADD_HEADERS: {
      break;
    }

    case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
      // MinnetRequest* req = minnet_request_data2(ctx, serv->req_obj);

      // lwsl_user("http LWS_CALLBACK_HTTP_BODY_COMPLETION\tis_h2=%i len: %zu, size: %zu\n", is_h2(wsi), len, buffer_WRITE(&req->body));

      MinnetCallback* cb = /*minnet_server.cb_http.ctx ? &minnet_server.cb_http :*/ serv->mount ? &serv->mount->callback : 0;
      MinnetBuffer b = BUFFER(buf);
      MinnetResponse* resp = request_handler(serv, cb);

      if(http_respond(wsi, &b, resp, ctx)) {
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
      MinnetRequest* req = minnet_request_data2(ctx, serv->req_obj);

      lwsl_user("http LWS_CALLBACK_HTTP_BODY\tis_h2=%i len: %zu, size: %zu\n", is_h2(wsi), len, buffer_WRITE(&req->body));

      if(len) {
        buffer_append(&req->body, in, len, ctx);

        js_dump_string(in, len, 80);
        puts("");
      }
      return 0;
    }

    case LWS_CALLBACK_HTTP: {
      MinnetHttpMount* mount;
      MinnetBuffer b = BUFFER(buf);
      JSValue* args = serv->args;
      char* path = in;
      int ret = 0;
      size_t mountpoint_len = 0;

      if(url && in && len < url_len && !strcmp((url_len - len) + url, in))
        mountpoint_len = url_len - len;

      lwsl_user("http " FG("%d") "%-25s" NC " wsi#%i mountpoint='%.*s' path='%s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, opaque->serial, (int)mountpoint_len, url, path);

      if(!opaque->req)
        opaque->req = request_new(ctx, path, url, method);

      if(!opaque->req->path[0])
        pstrcpy(opaque->req->path, sizeof(opaque->req->path), path);

      if(!(serv->mount = mount_find(url, mountpoint_len)))
        serv->mount = mount_find(url, 0);

      serv->h2 = is_h2(wsi);

      if((mount = serv->mount)) {
        size_t mlen = strlen(mount->mnt);
        assert(!strncmp(url, mount->mnt, mlen));
        assert(!strcmp(url + mlen, path));

        lwsl_user("http " FG("%d") "%-25s" NC " mount: mnt='%s', org='%s', pro='%s', origin_protocol='%s'\n",
                  22 + (reason * 2),
                  lws_callback_name(reason) + 13,
                  mount->mnt,
                  mount->org,
                  mount->pro,
                  ((const char*[]){"HTTP", "HTTPS", "FILE", "CGI", "REDIR_HTTP", "REDIR_HTTPS", "CALLBACK"})[(uintptr_t)mount->lws.origin_protocol]);
      }

      args[0] = serv->req_obj = minnet_request_wrap(ctx, opaque->req);

      if(!JS_IsObject(args[1]))
        args[1] = minnet_response_new(ctx, url, 200, TRUE, "text/html");

      MinnetRequest* req = opaque->req;
      MinnetResponse* resp = minnet_response_data2(ctx, args[1]);

      lwsl_user("http " FG("%d") "%-25s" NC " req=%p, header=%zu\n", 22 + (reason * 2), lws_callback_name(reason) + 13, req, buffer_WRITE(&req->headers));

      ++req->ref_count;
      MinnetCallback* cb = &minnet_server.cb_http;

      if(mount && (mount->lws.origin_protocol == LWSMPRO_FILE || (mount->lws.origin_protocol == LWSMPRO_CALLBACK && mount->lws.origin))) {

        if((ret = serve_file(wsi, path, mount, resp, ctx))) {

          lwsl_user("http " FG("%d") "%-25s" NC " serve_file FAIL %d", 22 + (reason * 2), lws_callback_name(reason) + 13, ret);
          JS_FreeValue(ctx, ws_obj);
          return 1;
        }
        if((ret = http_respond(wsi, &b, resp, ctx))) {

          lwsl_user("http " FG("%d") "%-25s" NC " http_respond FAIL %d", 22 + (reason * 2), lws_callback_name(reason) + 13, ret);
          JS_FreeValue(ctx, ws_obj);
          return 1;
        }

      } else if(mount && mount->lws.origin_protocol == LWSMPRO_CALLBACK) {

        cb = &mount->callback;

        if(req->method == METHOD_GET || is_h2(wsi)) {
          resp = request_handler(serv, cb);

          if(cb && cb->ctx) {
            JSValue gen = minnet_emit_this(cb, ws_obj, 2, args);
            assert(js_is_iterator(ctx, gen));
            lwsl_user("http " FG("%d") "%-25s" NC " gen=%s", 22 + (reason * 2), lws_callback_name(reason) + 13, JS_ToCString(ctx, gen));

            serv->generator = gen;
            serv->next = JS_UNDEFINED;
            serv->done = FALSE;
          } else {

            lwsl_user("http " FG("%d") "%-25s" NC " url=%s path=%s mountpoint=%.*s\n", 22 + (reason * 2), lws_callback_name(reason) + 13, url, path, (int)mountpoint_len, url);
            if(lws_http_transaction_completed(wsi))
              return -1;
          }
          if(http_respond(wsi, &b, resp, ctx)) {
            JS_FreeValue(ctx, ws_obj);
            return 1;
          }
        }
      } else {
        lwsl_user("http NOT FOUND\turl=%s path=%s mountpoint=%.*s\n", url, path, (int)mountpoint_len, url);
        break;

        /* if(lws_add_http_common_headers(wsi, HTTP_STATUS_NOT_FOUND, "text/html", LWS_ILLEGAL_HTTP_CONTENT_LEN, &b.write, b.end))
           return 1;

         if(lws_finalize_write_http_header(wsi, b.start, &b.write, b.end))
           return 1;

         if(lws_http_transaction_completed(wsi))
           return 1;*/
      }

      /*      if(lws_finalize_write_http_header(wsi, b.start, &b.write, b.end))
              return 1;
      */
      if(req->method == METHOD_GET || is_h2(wsi))
        lws_callback_on_writable(wsi);

      JS_FreeValue(ctx, ws_obj);

      return ret;
    }

    case LWS_CALLBACK_HTTP_WRITEABLE: {
      MinnetBuffer b = BUFFER(buf);

      MinnetResponse* resp = minnet_response_data2(minnet_server.ctx, serv->resp_obj);
      // MinnetRequest* req = minnet_request_data2(minnet_server.ctx, serv->req_obj);

      if(!serv->done)
        lwsl_user("http " FG("%d") "%-25s" NC " wsi#%i h2=%u mnt=%s remain=%td type=%s url=%s",
                  22 + (reason * 2),
                  lws_callback_name(reason) + 13,
                  opaque->serial,
                  serv->h2,
                  serv->mount ? serv->mount->mnt : 0,
                  resp ? buffer_REMAIN(&resp->body) : 0,
                  resp ? resp->type : 0,
                  resp ? resp->url : 0);

      if(JS_IsObject(serv->generator)) {
        JSValue ret = JS_UNDEFINED;

        while(!serv->done) {
          ret = js_iterator_next(minnet_server.ctx, serv->generator, &serv->next, &serv->done, 0, 0);

          if(JS_IsException(ret)) {
            JSValue exception = JS_GetException(ctx);
            fprintf(stderr, "Exception: %s\n", JS_ToCString(ctx, exception));
            serv->done = TRUE;
          } else if(!serv->done) {
            JSBuffer out = js_buffer_from(minnet_server.ctx, ret);
            /*    lwsl_user("http " FG("%d") "%-25s" NC " size=%zu", 22 + (reason * 2), lws_callback_name(reason) + 13, out.size);*/
            buffer_append(&resp->body, out.data, out.size, ctx);
            js_buffer_free(&out, minnet_server.ctx);
          }
          lwsl_user("http " FG("%d") "%-25s" NC " wsi#%i done=%i write=%zu", 22 + (reason * 2), lws_callback_name(reason) + 13, opaque->serial, serv->done, buffer_WRITE(&resp->body));
        }

      } else if(!buffer_WRITE(&resp->body)) {
        static int unhandled;

        if(!unhandled++)
          lwsl_user("http " FG("%d") "%-25s" NC " unhandled", 22 + (reason * 2), lws_callback_name(reason) + 13);

        break;
      } else {
        serv->done = TRUE;
      }

      //  buffer_dump("&resp->body", &resp->body);
      http_writable(wsi, resp, serv->done);
      break;
    }

    case LWS_CALLBACK_HTTP_FILE_COMPLETION: {
      //  printf("http \033[38;5;171mHTTP_FILE_COMPLETION\033[0m in = '%.*s' url = %s\n", len, in, url);
      break;
    }

    case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_HTTP_DROP_PROTOCOL: {
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

#include <sys/types.h>
#include <cutils.h>
#include <ctype.h>
#include <libgen.h>
#include <assert.h>

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
vhost_options_free(JSContext* ctx, MinnetVhostOptions* vo) {

  if(vo->name)
    js_free(ctx, (void*)vo->name);
  if(vo->value)
    js_free(ctx, (void*)vo->value);

  js_free(ctx, (void*)vo);
}

MinnetHttpMount*
mount_create(JSContext* ctx, const char* mountpoint, const char* origin, const char* def, enum lws_mount_protocols origin_proto) {
  MinnetHttpMount* m = js_mallocz(ctx, sizeof(MinnetHttpMount));

  // printf("mount_create mnt=%-10s org=%-10s def=%s\n", mountpoint, origin, def);

  m->lws.mountpoint = js_strdup(ctx, mountpoint);
  m->lws.origin = origin ? js_strdup(ctx, origin) : 0;
  m->lws.def = def ? js_strdup(ctx, def) : 0;
  m->lws.protocol = origin_proto == LWSMPRO_CALLBACK ? "http" : "defprot";
  m->lws.origin_protocol = origin_proto;
  m->lws.mountpoint_len = strlen(mountpoint);

  return m;
}

MinnetHttpMount*
mount_new(JSContext* ctx, JSValueConst obj, const char* key) {
  MinnetHttpMount* ret;
  JSValue mnt = JS_UNDEFINED, org = JS_UNDEFINED, def = JS_UNDEFINED;

  if(JS_IsArray(ctx, obj)) {
    mnt = JS_GetPropertyUint32(ctx, obj, 0);
    org = JS_GetPropertyUint32(ctx, obj, 1);
    def = JS_GetPropertyUint32(ctx, obj, 2);
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

  // printf("mount_new '%s'\n", path);

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

struct http_mount*
mount_find(MinnetHttpMount* mounts, const char* x, size_t n) {
  struct lws_http_mount *p, *m = 0;
  int protocol = n == 0 ? LWSMPRO_CALLBACK : LWSMPRO_HTTP;
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
http_server_respond(struct lws* wsi, MinnetBuffer* buf, MinnetResponse* resp, JSContext* ctx) {

  struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);
  int is_ssl = lws_is_ssl(wsi);

  lwsl_user("http " FG("198") "%-38s" NC " wsi#%" PRId64 " url=%s status=%d type=%s length=%zu", "RESPOND", opaque->serial, resp->url, resp->status, resp->type, buffer_HEAD(&resp->body));

  // resp->read_only = TRUE;

  if(lws_add_http_common_headers(wsi, resp->status, resp->type, is_ssl ? LWS_ILLEGAL_HTTP_CONTENT_LEN : buffer_HEAD(&resp->body), &buf->write, buf->end)) {
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

        lwsl_user("HTTP header %s = %.*s", prop, (int)(len - n), &x[n]);

        if((lws_add_http_header_by_name(wsi, (const unsigned char*)prop, (const unsigned char*)&x[n], len - n, &buf->write, buf->end)))
          JS_ThrowInternalError(ctx, "lws_add_http_header_by_name failed");
        js_free(ctx, (void*)prop);
      }
    }
  }
  int ret = lws_finalize_write_http_header(wsi, buf->start, &buf->write, buf->end);

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

  if(path[0] == '\0')
    path = mount->def;

  /*{
    char disposition[1024];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", basename(path));
    headers_set(ctx, &resp->headers, "Content-Disposition", disposition);
  }*/

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

  lwsl_user("serve_file path=%s mount=%.*s length=%td", path, mount->lws.mountpoint_len, mount->lws.mountpoint, buffer_HEAD(&resp->body));

  return 0;
}

int
http_server_writable(struct lws* wsi, struct http_response* resp, BOOL done) {
  struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);
  enum lws_write_protocol n, p;
  size_t remain;
  ssize_t ret = 0;

  n = done ? LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP;
  /*  if(!buffer_BYTES(&resp->body) && is_h2(wsi)) buffer_append(&resp->body, "\nXXXXXXXXXXXXXX", 1, ctx);*/

  if((remain = buffer_BYTES(&resp->body))) {
    uint8_t* x = resp->body.read;
    size_t l = is_h2(wsi) ? (remain > 1024 ? 1024 : remain) : remain;

    if(l > 0) {
      p = (remain - l) > 0 ? LWS_WRITE_HTTP : n;
      ret = lws_write(wsi, x, l, p);
      lwsl_user("lws_write wsi#%" PRIi64 " len=%zu final=%d ret=%zd", opaque->serial, l, p == LWS_WRITE_HTTP_FINAL, ret);
      if(ret > 0)
        buffer_skip(&resp->body, ret);
    }
  }

  lwsl_user("http_server_writable wsi#%" PRIi64 " done=%i remain=%zu final=%d", opaque->serial, done, buffer_BYTES(&resp->body), p == LWS_WRITE_HTTP_FINAL);

  if(p == LWS_WRITE_HTTP_FINAL) {
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
http_server_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE];
  MinnetHttpMethod method = -1;
  MinnetSession* session = user;
  MinnetServer* server = session ? session->server : lws_context_user(lws_get_context(wsi));
  JSContext* ctx = server ? server->context.js : 0;
  JSValue ws_obj = minnet_ws_object(ctx, wsi);
  struct wsi_opaque_user_data* opaque = ctx ? lws_opaque(wsi, ctx) : lws_get_opaque_user_data(wsi);
  char* url = 0;
  size_t url_len;

  if(session) {
    if(reason == LWS_CALLBACK_FILTER_HTTP_CONNECTION || reason == LWS_CALLBACK_HTTP_CONFIRM_UPGRADE) {
      session->serial = opaque->serial;
      session->h2 = is_h2(wsi);
    }
  }

  if(opaque->req) {
    url = opaque->req->url;
    method = opaque->req->method;
  } else {
    // url = lws_uri_and_method(wsi, ctx, &method);
  }
  url_len = url ? strlen(url) : 0;

  lwsl_user("HTTP " FG("%d") "%-38s" NC " wsi#%" PRId64 " url='%.*s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, opaque->serial, (int)url_len, url);

  switch(reason) {
    case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CHECK_ACCESS_RIGHTS:
    case LWS_CALLBACK_PROTOCOL_INIT: {
      break;
    }

    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: {
      JSValueConst args[2] = {ws_obj, JS_NULL};

      if(!opaque->req)
        opaque->req = request_new(server->context.js, in, url, method);

      int num_hdr = headers_get(ctx, &opaque->req->headers, wsi);

      lwsl_user("http " FGC(171, "%-38s") " fd=%i, num_hdr=%i\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), num_hdr);

      /*return 1;*/
      break;
    }

    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {

      if(!opaque->req)
        opaque->req = request_new(ctx, 0, url, method);

      /*  MinnetBuffer* h = &opaque->req->headers;
        int num_hdr = headers_get(ctx, h, wsi);
        lwsl_user("http " FGC(171, "%-38s") " %s\n", lws_callback_name(reason) + 13, request_dump(opaque->req, ctx));
  */
      // return 0;
      break;
    }

    case LWS_CALLBACK_ADD_HEADERS: {
      break;
    }

    case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
      MinnetCallback* cb = session->mount ? &session->mount->callback : 0;
      MinnetBuffer b = BUFFER(buf);
      MinnetResponse* resp = request_handler(session, cb);

      if(cb && cb->ctx) {
        JSValue ret = minnet_emit_this(cb, ws_obj, 2, session->args);

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

      lwsl_user("http LWS_CALLBACK_HTTP_BODY\tis_h2=%i len: %zu, size: %zu\n", is_h2(wsi), len, buffer_HEAD(&req->body));

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
      JSValue* args = session->args;
      char* path = in;
      int ret = 0;
      size_t mountpoint_len = 0;

      if(url && in && len < url_len && !strcmp((url_len - len) + url, in))
        mountpoint_len = url_len - len;

      lwsl_user("http " FG("%d") "%-38s" NC " wsi#%" PRId64 " mountpoint='%.*s' path='%s'\n", 22 + (reason * 2), lws_callback_name(reason) + 13, opaque->serial, (int)mountpoint_len, url, path);

      if(!opaque->req)
        opaque->req = request_new(ctx, path, url, method);

      if(!opaque->req->headers.write) {
        int num_hdr = headers_get(ctx, &opaque->req->headers, wsi);
      }

      if(!opaque->req->path[0])
        pstrcpy(opaque->req->path, sizeof(opaque->req->path), path);

      session->mount = mount_find(server->context.info.mounts, path, 0);
      if(url && !session->mount)
        if(!(session->mount = mount_find(server->context.info.mounts, url, mountpoint_len)))
          session->mount = mount_find(server->context.info.mounts, url, 0);

      session->h2 = is_h2(wsi);

      if((mount = session->mount)) {
        size_t mlen = strlen(mount->mnt);
        assert(!strncmp(url, mount->mnt, mlen));
        assert(!strcmp(url + mlen, path));

        lwsl_user("http " FG("%d") "%-38s" NC " mount: mnt='%s', org='%s', pro='%s', origin_protocol='%s'\n",
                  22 + (reason * 2),
                  lws_callback_name(reason) + 13,
                  mount->mnt,
                  mount->org,
                  mount->pro,
                  ((const char*[]){"HTTP", "HTTPS", "FILE", "CGI", "REDIR_HTTP", "REDIR_HTTPS", "CALLBACK"})[(uintptr_t)mount->lws.origin_protocol]);
      }

      args[0] = session->req_obj = minnet_request_wrap(ctx, opaque->req);

      if(!JS_IsObject(args[1]))
        args[1] = minnet_response_new(ctx, url, method == METHOD_POST ? 201 : 200, TRUE, "text/html");

      MinnetRequest* req = opaque->req;
      MinnetResponse* resp = minnet_response_data2(ctx, args[1]);

      lwsl_user("http " FG("%d") "%-38s" NC " req=%p, header=%zu\n", 22 + (reason * 2), lws_callback_name(reason) + 13, req, buffer_HEAD(&req->headers));

      ++req->ref_count;
      MinnetCallback* cb = &server->cb.http;

      if(mount && (mount->lws.origin_protocol == LWSMPRO_FILE || (mount->lws.origin_protocol == LWSMPRO_CALLBACK && mount->lws.origin))) {

        if((ret = serve_file(wsi, path, mount, resp, ctx))) {

          lwsl_user("http " FG("%d") "%-38s" NC " serve_file FAIL %d", 22 + (reason * 2), lws_callback_name(reason) + 13, ret);
          JS_FreeValue(ctx, ws_obj);
          return 1;
        }
        if((ret = http_server_respond(wsi, &b, resp, ctx))) {

          lwsl_user("http " FG("%d") "%-38s" NC " http_server_respond FAIL %d", 22 + (reason * 2), lws_callback_name(reason) + 13, ret);
          JS_FreeValue(ctx, ws_obj);
          return 1;
        }

      } else if(mount && mount->lws.origin_protocol == LWSMPRO_CALLBACK) {

        cb = &mount->callback;

        if(req->method == METHOD_GET /* || is_h2(wsi)*/) {
          resp = request_handler(session, cb);

          if(cb && cb->ctx) {
            JSValue gen = minnet_emit_this(cb, ws_obj, 2, args);
            assert(js_is_iterator(ctx, gen));
            lwsl_user("http " FG("%d") "%-38s" NC " gen=%s", 22 + (reason * 2), lws_callback_name(reason) + 13, JS_ToCString(ctx, gen));

            session->generator = gen;
            session->next = JS_UNDEFINED;
          } else {

            lwsl_user("http " FG("%d") "%-38s" NC " url=%s path=%s mountpoint=%.*s\n", 22 + (reason * 2), lws_callback_name(reason) + 13, url, path, (int)mountpoint_len, url);
            if(lws_http_transaction_completed(wsi))
              return -1;
          }
          if(http_server_respond(wsi, &b, resp, ctx)) {
            JS_FreeValue(ctx, ws_obj);
            return 1;
          }
        }
      } else {
        lwsl_user("http NOT FOUND\turl=%s path=%s mountpoint=%.*s\n", url, path, (int)mountpoint_len, url);
        break;
      }
      if(req->method == METHOD_GET || is_h2(wsi))
        (wsi);

      JS_FreeValue(ctx, ws_obj);

      return ret;
    }

    case LWS_CALLBACK_HTTP_WRITEABLE: {
      MinnetBuffer b = BUFFER(buf);
      MinnetResponse* resp = minnet_response_data2(server->context.js, session->resp_obj);
      BOOL done = FALSE;

      lwsl_user("http-writeable " FG("%d") "%-38s" NC " wsi#%" PRId64 " h2=%u mnt=%s remain=%td type=%s url=%s",
                22 + (reason * 2),
                lws_callback_name(reason) + 13,
                opaque->serial,
                session->h2,
                session->mount ? session->mount->mnt : 0,
                resp ? buffer_BYTES(&resp->body) : 0,
                resp ? resp->type : 0,
                resp ? resp->url : 0);

      if(JS_IsObject(session->generator)) {
        JSValue ret = JS_UNDEFINED;

        while(!done) {
          ret = js_iterator_next(server->context.js, session->generator, &session->next, &done, 0, 0);

          if(JS_IsException(ret)) {
            JSValue exception = JS_GetException(ctx);
            fprintf(stderr, "Exception: %s\n", JS_ToCString(ctx, exception));
            done = TRUE;
          } else if(!done) {
            JSBuffer out = js_buffer_from(server->context.js, ret);
            lwsl_user("http " FG("%d") "%-38s" NC " size=%zu", 22 + (reason * 2), lws_callback_name(reason) + 13, out.size);
            buffer_append(&resp->body, out.data, out.size, ctx);
            js_buffer_free(&out, server->context.js);
          }
          lwsl_user("http " FG("%d") "%-38s" NC " wsi#%" PRId64 " done=%i write=%zu", 22 + (reason * 2), lws_callback_name(reason) + 13, opaque->serial, done, buffer_HEAD(&resp->body));

          if(opaque->status == OPEN) {
            if(http_server_respond(wsi, &b, resp, ctx)) {
              JS_FreeValue(ctx, ws_obj);
              return 1;
            }
            opaque->status = CLOSING;
          }
        }

      } else if(!buffer_HEAD(&resp->body)) {
        static int unhandled;

        if(!unhandled++)
          lwsl_user("http " FG("%d") "%-38s" NC " unhandled", 22 + (reason * 2), lws_callback_name(reason) + 13);

        break;
      } else {
        done = TRUE;
      }

      http_server_writable(wsi, resp, done);
      break;
    }

    case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
    case LWS_CALLBACK_HTTP_DROP_PROTOCOL: {
      break;
    }

    case LWS_CALLBACK_HTTP_FILE_COMPLETION: {
      return 0;
    }

    case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
    case LWS_CALLBACK_CLOSED_HTTP: {
      lwsl_user("http " FG("%d") "%-38s" NC " wsi#%" PRId64, 22 + (reason * 2), lws_callback_name(reason) + 13, opaque->serial);
      if(session) {
        JS_FreeValue(server->context.js, session->req_obj);
        session->req_obj = JS_UNDEFINED;
        JS_FreeValue(server->context.js, session->resp_obj);
        session->resp_obj = JS_UNDEFINED;
      }
      break;
    }
    default: {
      minnet_lws_unhandled(__func__, reason);
      break;
    }
  }
  int ret = 0;
  if(reason != LWS_CALLBACK_HTTP_WRITEABLE && (reason < LWS_CALLBACK_HTTP_BIND_PROTOCOL || reason > LWS_CALLBACK_CHECK_ACCESS_RIGHTS)) {
    lwsl_user("http " FG("%d") "%-38s" NC " wsi#%" PRId64 " fd=%i is_h2=%i is_ssl=%i url=%s method=%s in='%.*s' ret=%d\n",
              22 + (reason * 2),
              lws_callback_name(reason) + 13,
              opaque->serial,
              lws_get_socket_fd(wsi),
              (session && session->h2) || is_h2(wsi),
              lws_is_ssl(wsi),
              url,
              method_name(method),
              (int)len,
              (char*)in,
              ret);
  }

  ret = lws_callback_http_dummy(wsi, reason, user, in, len);

  return ret;
}

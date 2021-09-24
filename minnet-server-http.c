#include <sys/types.h>
#include <cutils.h>
#include <ctype.h>

#include "jsutils.h"
#include "minnet-websocket.h"
#include "minnet-server.h"
#include "minnet-server-http.h"
#include "minnet-response.h"
#include "minnet-request.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

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
  struct lws_http_mount *ptr, *m = 0;
  int protocol = n == 0 ? LWSMPRO_CALLBACK : LWSMPRO_HTTP;
  size_t l = 0;
  if(n == 0)
    n = strlen(x);
  if(protocol == LWSMPRO_CALLBACK && x[0] == '/') {
    x++;
    n--;
  }
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
  // printf("RESPOND\tstatus=%d type=%s\n", resp->status, resp->type);

  resp->read_only = TRUE;

  if(lws_add_http_common_headers(wsi, resp->status, resp->type, LWS_ILLEGAL_HTTP_CONTENT_LEN, &buf->write, buf->end))
    return 1;
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
  /*  list_for_each(el, &resp->headers) {
      struct http_header* hdr = list_entry(el, struct http_header, link);

      if((lws_add_http_header_by_name(wsi, (const unsigned char*)hdr->name, (const unsigned char*)hdr->value, strlen(hdr->value), &buf->write, buf->end)))
        JS_ThrowInternalError(minnet_server.cb_http.ctx, "lws_add_http_header_by_name failed");
    }*/

  if(lws_finalize_write_http_header(wsi, buf->start, &buf->write, buf->end))
    return 1;

  return 0;
}

static MinnetResponse*
request_handler(MinnetSession* serv, MinnetCallback* cb) {
  MinnetResponse* resp = minnet_response_data2(minnet_server.ctx, serv->resp_obj);

  if(cb->ctx) {
    JSValue ret = minnet_emit_this(cb, serv->ws_obj, 2, serv->args);
    if(JS_IsObject(ret) && minnet_response_data2(cb->ctx, ret)) {
      JS_FreeValue(cb->ctx, serv->args[1]);
      serv->args[1] = ret;
      resp = minnet_response_data2(cb->ctx, ret);
      response_dump(resp);
    } else {
      JS_FreeValue(cb->ctx, ret);
    }
  }

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

  return 0;
}

int
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

int
http_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  JSContext* ctx = minnet_server.ctx;
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE];
  MinnetHttpMethod method = METHOD_GET;
  MinnetSession* serv = user; // get_context(user, wsi);
  JSValue ws_obj = minnet_ws_object(ctx, wsi);
  struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);
  //  MinnetWebsocket* ws = minnet_ws_data2(ctx, ws_obj);
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

  /*if(url || (in && *(char*)in) || (path && *path)) */

  if(reason != LWS_CALLBACK_HTTP_WRITEABLE)
    lwsl_user("http %-25s fd=%i, is_h2=%i url=%s in='%.*s' path=%s mountpoint=%.*s\n",
              lws_callback_name(reason) + 13,
              lws_get_socket_fd(lws_get_network_wsi(wsi)),
              is_h2(wsi),
              url,
              (int)len,
              (char*)in,
              path,
              (int)mountpoint_len,
              mountpoint);

  switch((int)reason) {
    case(int)LWS_CALLBACK_ESTABLISHED: {
      case(int)LWS_CALLBACK_CHECK_ACCESS_RIGHTS:
      case(int)LWS_CALLBACK_PROTOCOL_INIT: break;
    }

    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: {
      JSValueConst args[2] = {ws_obj, JS_NULL};

      if(minnet_server.cb_connect.ctx) {
        opaque->req = request_new(minnet_server.cb_connect.ctx, path, url, method);
        int num_hdr = http_headers(ctx, &opaque->req->headers, wsi);
        lwsl_user("http \033[38;5;171m%-25s\033[0m fd=%i, num_hdr=%i\n", lws_callback_name(reason) + 13, lws_get_socket_fd(lws_get_network_wsi(wsi)), num_hdr);
        args[1] = minnet_request_wrap(minnet_server.cb_connect.ctx, opaque->req);
        minnet_emit_this(&minnet_server.cb_connect, ws_obj, 2, args);
        JS_FreeValue(ctx, args[1]);
      }
      // printf("http \033[38;5;171m%-25s\033[0m wsi=%p, ws=%p, req=%p, in='%.*s', path=%s, url=%s, opaque=%p\n", lws_callback_name(reason) + 13, wsi, ws, opaque->req, (int)len, (char*)in, path, url,
      // opaque);

      break;
    }

    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {

      if(!opaque->req)
        opaque->req = request_new(ctx, path, url, method);

     // int num_hdr = http_headers(ctx, &opaque->req->headers, wsi);
      // printf("http \033[38;5;171m%-25s\033[0m num_hdr = %i, offset = %zu, url=%s, req=%p\n", lws_callback_name(reason) + 13, num_hdr, buffer_OFFSET(&opaque->req->headers), url, opaque->req);
      // buffer_free(&opaque->req->headers, JS_GetRuntime(ctx));
      break;
    }

    case LWS_CALLBACK_ADD_HEADERS: {
      /*struct lws_process_html_args* args = (struct lws_process_html_args*)in;
      lwsl_user("http LWS_CALLBACK_ADD_HEADERS args: %.*s\n", args->len, args->p);*/
      break;
    }

    case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
      MinnetRequest* req = minnet_request_data2(ctx, serv->req_obj);

      lwsl_user("http LWS_CALLBACK_HTTP_BODY_COMPLETION\tis_h2=%i len: %zu, size: %zu\n", is_h2(wsi), len, buffer_OFFSET(&req->body));

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

      lwsl_user("http LWS_CALLBACK_HTTP_BODY\tis_h2=%i len: %zu, size: %zu\n", is_h2(wsi), len, buffer_OFFSET(&req->body));

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
      lwsl_user("http %-25s fd=%i, is_h2=%i, req=%p, url=%s, mnt=%s\n", "HTTP", lws_get_socket_fd(lws_get_network_wsi(wsi)), is_h2(wsi), opaque->req, url, mount->org);

      //  if(!JS_IsObject(args[0]))

      if(!opaque->req)
        opaque->req = request_new(ctx, path, lws_get_uri(wsi, ctx, WSI_TOKEN_GET_URI), METHOD_GET);

      args[0] = serv->req_obj = minnet_request_wrap(ctx, opaque->req);

      if(!JS_IsObject(args[1]))
        args[1] = minnet_response_new(ctx, url, 200, TRUE, "text/html");

      MinnetRequest* req = opaque->req;
      MinnetResponse* resp = minnet_response_data2(ctx, args[1]);

      lwsl_user("http \x1b[38;5;87m%-25s\x1b[0m req=%p, header=%zu\n", "HTTP", req, buffer_OFFSET(&req->headers));

      ++req->ref_count;

      if(mount && (mount->lws.origin_protocol == LWSMPRO_FILE || (mount->lws.origin_protocol == LWSMPRO_CALLBACK && mount->lws.origin))) {

        if((ret = serve_file(wsi, path, mount, resp, ctx)) == 0) {
          if(http_respond(wsi, &b, resp, ctx)) {
            JS_FreeValue(ctx, ws_obj);
            return 1;
          }
        }

      } else if(mount && mount->lws.origin_protocol == LWSMPRO_CALLBACK) {

        cb = &mount->callback;

        if(req->method == METHOD_GET || is_h2(wsi)) {
          resp = request_handler(serv, &minnet_server.cb_http);

          if(cb && cb->ctx) {
            JSValue ret = minnet_emit_this(cb, ws_obj, 2, args);
            assert(js_is_iterator(ctx, ret));
            serv->generator = ret;
          } else {

            lwsl_user("http NO CALLBACK\turl=%s path=%s mountpoint=%s\n", url, path, mountpoint);
            if(lws_http_transaction_completed(wsi))
              return -1;
          }
          if(http_respond(wsi, &b, resp, ctx)) {
            JS_FreeValue(ctx, ws_obj);
            return 1;
          }
        }
      } else {
        lwsl_user("http NOT FOUND\turl=%s path=%s mountpoint=%s\n", url, path, mountpoint);
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

      MinnetResponse* resp = minnet_response_data2(minnet_server.ctx, serv->resp_obj);
     // MinnetRequest* req = minnet_request_data2(minnet_server.ctx, serv->req_obj);
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
        static int unhandled;

        if(!unhandled++)
          lwsl_user("http WRITABLE unhandled\n");

        break;
      } else {
        done = TRUE;
      }

      return http_writable(wsi, resp, done);
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

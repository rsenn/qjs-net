#include <sys/types.h>
#include <cutils.h>

#include "buffer.h"
#include "jsutils.h"
#include "minnet-websocket.h"
#include "minnet-server.h"
#include "minnet-server-http.h"
#include "minnet-response.h"
#include "minnet-request.h"

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
minnet_http_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  JSContext* ctx = minnet_server.ctx;
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE];
  MinnetHttpMethod method = METHOD_GET;
  MinnetSession* serv = user; // get_context(user, wsi);
  JSValue ws_obj = minnet_ws_object(ctx, wsi);
  struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);
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

  // if(url || (in && *(char*)in) || (path && *path)) printf("http %s\tis_h2=%i url=%s in='%.*s' path=%s mountpoint=%.*s\n", lws_callback_name(reason) + 13, is_h2(wsi), url, len, in, path,
  // mountpoint_len, mountpoint);

  switch((int)reason) {
    case(int)LWS_CALLBACK_ESTABLISHED: {
      case(int)LWS_CALLBACK_CHECK_ACCESS_RIGHTS:
      case(int)LWS_CALLBACK_PROTOCOL_INIT: break;
    }

    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: {
      JSValueConst args[2] = {ws_obj, JS_NULL};

      if(minnet_server.cb_connect.ctx) {
        opaque->req = request_new(minnet_server.cb_connect.ctx, path, url, method);
        int num_hdr = headers(ctx, &opaque->req->header, wsi);
        //  printf("http \033[38;5;171m%s\033[0m num_hdr=%i\n", lws_callback_name(reason) + 13, num_hdr);
        args[1] = minnet_request_wrap(minnet_server.cb_connect.ctx, opaque->req);
        minnet_emit_this(&minnet_server.cb_connect, ws_obj, 2, args);
        JS_FreeValue(ctx, args[1]);
      }
      // printf("http \033[38;5;171m%s\033[0m wsi=%p, ws=%p, req=%p, in='%.*s', path=%s, url=%s, opaque=%p\n", lws_callback_name(reason) + 13, wsi, ws, opaque->req, (int)len, (char*)in, path, url,
      // opaque);

      break;
    }

    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {

      if(!opaque->req)
        opaque->req = request_new(ctx, path, url, method);

      int num_hdr = headers(ctx, &opaque->req->header, wsi);
      printf("http \033[38;5;171m%s\033[0m num_hdr = %i, offset = %zu, url=%s, req=%p\n", lws_callback_name(reason) + 13, num_hdr, buffer_OFFSET(&opaque->req->header), url, opaque->req);
      // buffer_free(&opaque->req->header, JS_GetRuntime(ctx));
      break;
    }

    case LWS_CALLBACK_ADD_HEADERS: {
      /*struct lws_process_html_args* args = (struct lws_process_html_args*)in;
      printf("http LWS_CALLBACK_ADD_HEADERS args: %.*s\n", args->len, args->p);*/
      break;
    }

    case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
      MinnetRequest* req = minnet_request_data(ctx, serv->req_obj);

      printf("http LWS_CALLBACK_HTTP_BODY_COMPLETION\tis_h2=%i len: %zu, size: %zu, in: ", is_h2(wsi), len, buffer_OFFSET(&req->body));

      MinnetCallback* cb = /*minnet_server.cb_http.ctx ? &minnet_server.cb_http :*/ serv->mount ? &serv->mount->callback : 0;
      MinnetBuffer b = BUFFER(buf);
      MinnetResponse* resp = request_handler(serv, cb);

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

      printf("http LWS_CALLBACK_HTTP_BODY\tis_h2=%i len: %zu, size: %zu\n", is_h2(wsi), len, buffer_OFFSET(&req->body));

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

      // printf("http LWS_CALLBACK_HTTP is_h2=%i, url=%s\n", is_h2(wsi), url);

      if(!(serv->mount = mount_find(mountpoint_len ? mountpoint : url, mountpoint_len ? mountpoint_len : 0)))
        serv->mount = mount_find(url, 0);

      if((mount = serv->mount)) {
        size_t mlen = strlen(mount->mnt);
        path = url;
        assert(!strncmp(url, mount->mnt, mlen));
        path += mlen;
      }

      //  if(!JS_IsObject(args[0]))
      args[0] = serv->req_obj = minnet_request_wrap(ctx, opaque->req);

      if(!JS_IsObject(args[1]))
        args[1] = minnet_response_new(ctx, url, 200, TRUE, "text/html");

      MinnetRequest* req = opaque->req;
      MinnetResponse* resp = minnet_response_data(ctx, args[1]);

      printf("http \x1b[38;5;87mLWS_CALLBACK_HTTP\x1b[0m req=%p, header=%zu\n", req, buffer_OFFSET(&req->header));

      ++req->ref_count;

      if(mount && (mount->lws.origin_protocol == LWSMPRO_FILE || (mount->lws.origin_protocol == LWSMPRO_CALLBACK && mount->lws.origin))) {

        if((ret = serve_file(wsi, path, mount, resp, ctx)) == 0) {
          if(respond(wsi, &b, resp)) {
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

            printf("http NO CALLBACK\turl=%s path=%s mountpoint=%s\n", url, path, mountpoint);
            if(lws_http_transaction_completed(wsi))
              return -1;
          }
          if(respond(wsi, &b, resp)) {
            JS_FreeValue(ctx, ws_obj);
            return 1;
          }
        }
      } else {
        printf("http NOT FOUND\turl=%s path=%s mountpoint=%s\n", url, path, mountpoint);
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
        static int unhandled;

        if(!unhandled++)
          printf("http WRITABLE unhandled\n");

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

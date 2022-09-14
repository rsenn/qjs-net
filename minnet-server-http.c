#include "minnet-server-http.h"
#include <assert.h>             // for assert
#include <ctype.h>              // for isspace
#include <cutils.h>             // for BOOL, FALSE, TRUE, pstrcpy
#include <inttypes.h>           // for PRId64, PRIi64
#include <libwebsockets.h>      // for lws_http_mount, wsi_tls, lws_call...
#include <stdint.h>             // for uint8_t, uint32_t, uintptr_t
#include <stdio.h>              // for printf, fseek, ftell, fclose, fopen
#include <string.h>             // for strlen, size_t, strncmp, strcmp, strstr
#include <sys/types.h>          // for ssize_t
#include "context.h"            // for struct context
#include "headers.h"            // for headers_tobuffer
#include "jsutils.h"            // for js_is_iterator, JSBuffer, js_buffer_...
#include "minnet-form-parser.h" // for form_parser, form_parser::(anonymous)
#include "minnet-generator.h"   // for MinnetGenerator, generator_close
#include "minnet-request.h"     // for MinnetRequest, http_request, minnet_...
#include "minnet-response.h"    // for http_response, http_response::(anony...
#include "minnet-server.h"      // for MinnetServer, server_exception, serv...
#include "minnet-url.h"         // for MinnetURL, url_string, url_set_path_len
#include "minnet-websocket.h"   // for lws_opaque, minnet_ws_wrap
#include "minnet.h"             // for LOGCB, LOG, minnet_lws_unhandled
#include "opaque.h"             // for wsi_opaque_user_data, OPEN
#include "quickjs.h"            // for JS_FreeValue, js_free, JS_FreeCString
#include "utils.h"              // for wsi_http2, FG, METHOD_GET, NC, byte_chr

int lws_hdr_simple_create(struct lws*, enum lws_token_indexes, const char*);

MinnetVhostOptions*
vhost_options_create(JSContext* ctx, const char* name, const char* value) {
  MinnetVhostOptions* vo = js_mallocz(ctx, sizeof(MinnetVhostOptions));

  // DEBUG("vhost_options_create %s %s\n", name, value);

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

MinnetVhostOptions*
vhost_options_fromentries(JSContext* ctx, JSValueConst arr) {
  uint32_t i, len = js_get_propertystr_uint32(ctx, arr, "length");
  MinnetVhostOptions *vo = 0, **voptr = &vo;

  for(i = 0; i < len; i++) {
    JSValue val = JS_GetPropertyUint32(ctx, arr, i);

    *voptr = vhost_options_new(ctx, val);
    voptr = &(*voptr)->next;

    JS_FreeValue(ctx, val);
  }

  return vo;
}

MinnetVhostOptions*
vhost_options_fromobj(JSContext* ctx, JSValueConst obj) {
  JSPropertyEnum* tab;
  uint32_t tab_len, i;
  MinnetVhostOptions *vo = 0, **voptr = &vo;

  if(JS_IsArray(ctx, obj))
    return vhost_options_fromentries(ctx, obj);

  if(JS_GetOwnPropertyNames(ctx, &tab, &tab_len, obj, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK))
    return 0;

  for(i = 0; i < tab_len; i++) {
    JSAtom prop = tab[i].atom;
    const char* name = JS_AtomToCString(ctx, prop);
    JSValue val = JS_GetProperty(ctx, obj, prop);
    const char* value = JS_ToCString(ctx, val);
    JS_FreeValue(ctx, val);

    *voptr = vhost_options_create(ctx, name, value);
    voptr = &(*voptr)->next;

    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, value);
  }
  js_free(ctx, tab);

  return vo;
}

void
vhost_options_dump(MinnetVhostOptions* vo) {

  uint32_t i = 0;
  while(vo) {
    i++;
    DEBUG("option %u %s = %s\n", i, vo->name, vo->value);

    vo = vo->next;
  }
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
    DEBUG("mount_create mnt=%-10s org=%-20s def=%-15s protocol=%-10s origin_protocol=%s\n",
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
    // opt = JS_GetPropertyUint32(ctx, obj, 4);
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

  // DEBUG("mount_new '%s'\n", path);

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
    enum lws_mount_protocols proto = plen == 0 ? LWSMPRO_CALLBACK : (plen == 5 && !strncmp(dest, "https", plen)) ? LWSMPRO_HTTPS : LWSMPRO_HTTP;

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
  int protocol = n == 0 ? LWSMPRO_CALLBACK : LWSMPRO_HTTP;
  size_t l = 0;

  // DEBUG("mount_find('%.*s')\n", (int)n, x);

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
      // DEBUG("mount_find x='%.*s' '%.*s'\n", (int)n, x, (int)len, mnt);

      if((len == n || (n > len && (x[len] == '/' || x[len] == '?'))) && !strncmp(x, mnt, n)) {
        m = p;
        /*   break;*/
      }
      if(n >= len && len >= l && !strncmp(mnt, x, MIN(len, n))) {
        m = p;
      }
    }
  }
  if(m) {
    DEBUG("mount_find org=%s mnt=%s cb.ctx=%p\n", ((struct http_mount*)m)->org, ((struct http_mount*)m)->mnt, ((struct http_mount*)m)->callback.ctx);
  }
  return (struct http_mount*)m;
}

struct http_mount*
mount_find_s(MinnetHttpMount* mounts, const char* x) {
  struct lws_http_mount *p, *m = 0;
  size_t n = strlen(x);

  for(p = (struct lws_http_mount*)mounts; p; p = (struct lws_http_mount*)p->mount_next) {
    const char* mnt = p->mountpoint;
    size_t len = p->mountpoint_len;

    // DEBUG("mount x='%.*s' '%.*s'\n", (int)n, x, (int)len, mnt);

    if(len == n && !strncmp(x, mnt, n)) {
      m = p;
      break;
    }

    if(len == 1 && mnt[0] == '/')
      m = p;

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

BOOL
mount_is_proxy(MinnetHttpMount const* m) {
  return m->lws.origin_protocol == LWSMPRO_HTTP || m->lws.origin_protocol == LWSMPRO_HTTPS;
}

int
http_server_respond(struct lws* wsi, ByteBuffer* buf, struct http_response* resp, JSContext* ctx, struct session_data* session) {
  struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);
  int is_ssl = wsi_tls(wsi);
  int h2 = wsi_http2(wsi);

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

  if(!h2) {
    BOOL done = FALSE;
    while(!done) http_server_generate(ctx, session, resp, &done);
  }

  if(lws_add_http_common_headers(wsi, resp->status, resp->type, is_ssl || h2 ? LWS_ILLEGAL_HTTP_CONTENT_LEN : buffer_HEAD(resp->body), &buf->write, buf->end)) {
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

        DEBUG("HTTP header %s = %.*s\n", prop, (int)(len - n), &x[n]);
        if((lws_add_http_header_by_name(wsi, (const unsigned char*)prop, (const unsigned char*)&x[n], len - n, &buf->write, buf->end)))
          JS_ThrowInternalError(ctx, "lws_add_http_header_by_name failed");
        js_free(ctx, (void*)prop);
      }
    }
  }
  int ret = lws_finalize_write_http_header(wsi, buf->start, &buf->write, buf->end);

  DEBUG("HTTP headers '%.*s'\n", (int)buffer_HEAD(buf), buf->start);

  /* {
     char* b = buffer_escaped(buf, ctx);
     lwsl_user("lws_finalize_write_http_header '%s' %td ret=%d", b, buf->write - buf->start, ret);
      js_free(ctx, b);
   }*/
  if(ret)
    return 2;

  return 0;
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

  DEBUG("serve_file path=%s mount=%s\n", path, mount->mnt);

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
  /*  if(!buffer_BYTES(resp->body) && wsi_http2(wsi)) buffer_append(resp->body, "\nXXXXXXXXXXXXXX", 1, ctx);*/

  if((remain = buffer_REMAIN(resp->body))) {
    uint8_t* x = resp->body->read;
    size_t l = wsi_http2(wsi) ? (remain > 1024 ? 1024 : remain) : remain;

    if(l > 0) {
      p = (remain - l) > 0 ? LWS_WRITE_HTTP : n;
      ret = lws_write(wsi, x, l, p);
      LOG("SERVER-HTTP", FG("%d") "%-38s" NC " wsi#%" PRIi64 " len=%zu final=%d ret=%zd", 112, __func__ + 12, opaque->serial, l, p == LWS_WRITE_HTTP_FINAL, ret);

      buffer_skip(resp->body, ret);
    }
  }

  remain = buffer_REMAIN(resp->body);

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
http_server_generate(JSContext* ctx, struct session_data* session, MinnetResponse* resp, BOOL* done_p) {
  // ByteBuffer b = BUFFER(buf);

  if(JS_IsObject(session->generator)) {
    JSValue ret = JS_UNDEFINED;
    JSBuffer out = JS_BUFFER(0, 0, 0);

    session->next = JS_UNDEFINED;

    DEBUG("LWS_CALLBACK_HTTP_WRITEABLE: %s\n", JS_ToCString(ctx, session->generator));

    while(!*done_p) {
      ret = js_iterator_next(ctx, session->generator, &session->next, done_p, 0, 0);

      if(JS_IsException(ret)) {
        JSValue exception = JS_GetException(ctx);
        js_error_print(ctx, exception);
        *done_p = TRUE;
      } else if(!*done_p) {
        out = js_buffer_new(ctx, ret);
        // LOGCB("HTTP-WRITEABLE", "size=%zu, out='%.*s'", out.size, (int)(out.size > 255 ? 255 : out.size), out.size > 255 ? &out.data[out.size - 255] : out.data);
        DEBUG("\x1b[2K\ryielded %.*s %zu\n", (int)(out.size > 255 ? 255 : out.size), out.size > 255 ? &out.data[out.size - 255] : out.data, out.size);

        if(!resp->generator)
          response_generator(resp, ctx);

        buffer_append(resp->body, out.data, out.size, ctx);
        js_buffer_free(&out, ctx);
        // break;
      }
    }

  } else {
    *done_p = TRUE;
  }
  // LOGCB("HTTP-WRITEABLE", "status=%s done=%i write=%zu", ((const char*[]){"CONNECTING", "OPEN", "CLOSING", "CLOSED"})[opaque->status], done, resp->body ? buffer_HEAD(resp->body) : 0);

  if(!resp->body || !buffer_HEAD(resp->body)) {
    static int unhandled;

    // if(!unhandled) LOGCB("HTTP", "unhandled %d", unhandled);
    unhandled++;
    return 0;
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
  return 0;
}

int
http_server_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  int ret = 0;
  uint8_t buf[LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE];
  MinnetServer* server = lws_context_user(lws_get_context(wsi));
  struct session_data* session = user;
  JSContext* ctx = server ? server->context.js : 0;
  struct wsi_opaque_user_data* opaque = lws_get_opaque_user_data(wsi);

  if(lws_reason_poll(reason)) {
    assert(server);
    return wsi_handle_poll(wsi, reason, &server->cb.fd, in);
  }

  if(reason == LWS_CALLBACK_HTTP_CONFIRM_UPGRADE) {
    if(session && session->serial != opaque->serial) {
      session->serial = opaque->serial;
      session->h2 = wsi_http2(wsi);
    }
  }

  if(!opaque && ctx)
    opaque = lws_opaque(wsi, ctx);

  assert(opaque);

  // if(reason != LWS_CALLBACK_HTTP_BODY)
  LOGCB("HTTP",
        "%s%sfd=%d in='%.*s' url=%s session#%d",
        wsi_http2(wsi) ? "h2, " : "http/1.1, ",
        wsi_tls(wsi) ? "TLS, " : "plain, ",
        lws_get_socket_fd(lws_get_network_wsi(wsi)),
        (int)MIN(32, len),
        (char*)in,
        opaque && opaque->req ? url_string(&opaque->req->url) : 0,
        session ? session->serial : 0);

  if(opaque->upstream) {
    if(reason == LWS_CALLBACK_FILTER_HTTP_CONNECTION) {
      printf("FILTER(2)\n");
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);
  }
  switch(reason) {
    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CHECK_ACCESS_RIGHTS:
    case LWS_CALLBACK_PROTOCOL_INIT:
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS:
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
    case LWS_CALLBACK_PROTOCOL_DESTROY: break;

    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: {
      /*if(!wsi_tls(wsi) && !strcmp(in, "h2c"))
        return -1;


      //int num_hdr = headers_tobuffer(ctx, &opaque->req->headers, wsi);

      LOGCB("HTTP", "fd=%i, num_hdr=%i", lws_get_socket_fd(lws_get_network_wsi(wsi)), num_hdr);
 */ break;
    }

    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {

      if((session->mount = mount_find((MinnetHttpMount*)server->context.info.mounts, in, len))) {
        if(mount_is_proxy(session->mount))
          lws_hdr_simple_create(wsi, WSI_TOKEN_HOST, "");
      }

      if(opaque->upstream) {
        printf("FILTER\n");
        return lws_callback_http_dummy(wsi, reason, user, in, len);
      }

      if(!opaque->req)
        opaque->req = request_fromwsi(wsi, ctx);

      if(in) {
        opaque->uri = in;
        opaque->uri_len = len ? len : strlen(in);

        url_set_path_len(&opaque->req->url, in, len, ctx);
      }

      url_set_protocol(&opaque->req->url, wsi_tls(wsi) ? "https" : "http");

      // LOGCB("HTTP", "len=%d, in=%.*s", (int)len, (int)len, (char*)in);
      break;
    }

    case LWS_CALLBACK_HTTP_BIND_PROTOCOL: {
      opaque->status = OPEN;

      if(opaque->req)
        url_set_protocol(&opaque->req->url, wsi_tls(wsi) ? "https" : "http");

      // LOGCB("HTTP", "url=%s", opaque->req ? url_string(&opaque->req->url) : 0);
      break;
    }

    case LWS_CALLBACK_ADD_HEADERS: {
      break;
    }

    case LWS_CALLBACK_HTTP_BODY: {

      if(opaque->upstream)
        return lws_callback_http_dummy(wsi, reason, user, in, len);

      MinnetRequest* req = minnet_request_data2(ctx, session->req_obj);

      session->in_body = TRUE;

      LOGCB("HTTP", "%slen: %zu parser: %p", wsi_http2(wsi) ? "h2, " : "", len, opaque->form_parser);

      if(len) {
        if(opaque->form_parser) {
          form_parser_process(opaque->form_parser, in, len);
        } else {
          if(!req->body)
            req->body = generator_new(ctx);
          generator_write(req->body, in, len);
        }
      }

      if(server->cb.read.ctx) {
        JSValue args[] = {JS_NewStringLen(server->cb.read.ctx, in, len)};
        JSValue ret = server_exception(server, callback_emit_this(&server->cb.read, session->req_obj, countof(args), args));
        JS_FreeValue(server->cb.read.ctx, ret);
      }

      break;
    }

    case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
      MinnetFormParser* fp;
      ByteBuffer b = BUFFER(buf);

      session->in_body = FALSE;

      LOGCB("HTTP", "%slen: %zu", wsi_http2(wsi) ? "h2, " : "", len);

      if((fp = opaque->form_parser)) {
        lws_spa_finalize(fp->spa);
        if(fp->cb.finalize.ctx) {
          JSValue ret = server_exception(server, callback_emit(&fp->cb.finalize, 0, 0));
          JS_FreeValue(fp->cb.finalize.ctx, ret);
        }
      }

      {
        JSCallback* cb = session->mount ? &session->mount->callback : 0;

        if(cb && cb->ctx) {
          JSValue ret = server_exception(server, callback_emit_this(cb, session->ws_obj, 2, session->args));

          assert(js_is_iterator(ctx, ret));
          session->generator = ret;
        }

        MinnetRequest* req = minnet_request_data2(ctx, session->req_obj);
        if(req->body && ctx) {
          DEBUG("POST body: %p\n", req->body);
          generator_close(req->body, ctx);
        }
      }

      if(server->cb.post.ctx) {
        JSValue args[] = {opaque->binary ? buffer_toarraybuffer(&opaque->req->body->buffer, server->cb.post.ctx) : buffer_tostring(&opaque->req->body->buffer, server->cb.post.ctx)};
        JSValue ret = server_exception(server, callback_emit_this(&server->cb.post, session->req_obj, countof(args), args));
        JS_FreeValue(server->cb.post.ctx, ret);
      }

      if(http_server_respond(wsi, &b, opaque->resp, ctx, session)) {
        JS_FreeValue(ctx, session->ws_obj);
        session->ws_obj = JS_NULL;
        // return 1;
      }

      lws_callback_on_writable(wsi);
      break;
    }

    case LWS_CALLBACK_HTTP: {
      MinnetRequest* req;
      ByteBuffer b = BUFFER(buf);
      JSValue* args = &session->ws_obj;
      char* path = in;
      size_t mountpoint_len = 0, pathlen = 0;

      if(!(req = opaque->req))
        req = opaque->req = request_fromwsi(wsi, ctx);

      if(opaque->uri)
        url_set_path_len(&req->url, opaque->uri, opaque->uri_len, ctx);

      //      assert(url_query(req->url));

      assert(req);
      assert(req->url.path);

      // DEBUG("req->url.path = '%s'\n", req->url.path);
      pathlen = req->url.path ? strlen(req->url.path) : 0;

      if(opaque->uri) {
        mountpoint_len = (char*)in - opaque->uri;

        // DEBUG("opaque->uri = '%.*s'\n", (int)opaque->uri_len, opaque->uri);
        // DEBUG("mountpoint_len = %zu\n", mountpoint_len);
      } else if(req->url.path && in && len < pathlen)
        mountpoint_len = pathlen - len;

      LOGCB("HTTP(1)", "mountpoint='%.*s' path='%s'", (int)mountpoint_len, req->url.path, path);

      // request_query(opaque->req, wsi, ctx);

      if(!opaque->req->headers.write) {
        /*int num_hdr =*/headers_tobuffer(ctx, &opaque->req->headers, wsi);
      }
      {
        MinnetHttpMount* mounts = (MinnetHttpMount*)server->context.info.mounts;

        if(!session->mount)
          if(req->url.path)
            session->mount = mount_find(mounts, req->url.path, mountpoint_len);
        if(!session->mount)
          if(path)
            session->mount = mount_find(mounts, path, 0);
        if(req->url.path && !session->mount)
          if(!(session->mount = mount_find(mounts, req->url.path, mountpoint_len)))
            session->mount = mount_find(mounts, req->url.path, 0);
      }

      session->h2 = wsi_http2(wsi);
      {
        MinnetHttpMount* mount;

        if((mount = session->mount)) {
          size_t mlen = strlen(mount->mnt);

          // DEBUG("mount->mnt = '%s'\n", mount->mnt);
          // DEBUG("mount->mnt = '%.*s'\n", (int)mlen, mount->mnt);

          assert(req->url.path);
          assert(mount->mnt);
          assert(mlen);
          // assert(!strncmp(req->url.path, mount->mnt, mlen));

          if(!strcmp(req->url.path + mlen, path)) {
            assert(!strcmp(req->url.path + mlen, path));

            LOGCB("HTTP(2)",
                  "mount: mnt='%s', org='%s', pro='%s', origin_protocol='%s'\n",
                  mount->mnt,
                  mount->org,
                  mount->pro,
                  ((const char*[]){"HTTP", "HTTPS", "FILE", "CGI", "REDIR_HTTP", "REDIR_HTTPS", "CALLBACK"})[(uintptr_t)mount->lws.origin_protocol]);
          }
        }
        session->req_obj = minnet_request_wrap(ctx, opaque->req);

        if(!JS_IsObject(session->ws_obj)) {
          if(opaque->ws)
            session->ws_obj = minnet_ws_wrap(ctx, opaque->ws);
        }

        if(!JS_IsObject(session->resp_obj))
          session->resp_obj = minnet_response_new(ctx, req->url, /*opaque->req->method == METHOD_POST ? 201 :*/ 200, 0, TRUE, "text/html");

        // MinnetRequest* req = opaque->req;
        MinnetResponse* resp = opaque->resp = minnet_response_data2(ctx, session->resp_obj);

        LOGCB("HTTP(3)", "req=%p, header=%zu", req, buffer_HEAD(&req->headers));

        request_dup(req);

        JSCallback* cb = &mount->callback;

        if(mount && !mount->callback.ctx)
          cb = 0;

        if(mount && ((mount->lws.origin_protocol == LWSMPRO_CALLBACK && (!cb || !cb->ctx)) ||
                     (mount->lws.origin_protocol == LWSMPRO_FILE || (mount->lws.origin_protocol == LWSMPRO_CALLBACK && mount->lws.origin)))) {

          ret = serve_file(wsi, path, mount, resp, ctx);

          if(ret) {
            LOGCB("HTTP(4)", "serve_file FAIL %d", ret);
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
              return 0;
           }*/
        }

        if(mount && mount->lws.origin_protocol == LWSMPRO_CALLBACK) {

          if(cb && cb->ctx) {
            if(req->method == METHOD_GET /* || wsi_http2(wsi)*/) {
              resp = session_response(session, cb);

              JSValue gen = server_exception(server, callback_emit_this(cb, session->ws_obj, 2, &args[1]));
              if(js_is_iterator(ctx, gen)) {
                assert(js_is_iterator(ctx, gen));
                LOGCB("HTTP(5)", "gen=%s", JS_ToCString(ctx, gen));

                session->generator = gen;
                session->next = JS_UNDEFINED;

                /* lws_callback_on_writable(wsi);
                 return 0;*/
              } else {
                LOGCB("HTTP(6)", "gen=%s", JS_ToCString(ctx, gen));
              }
            }
          }

          /*     LOGCB("HTTP", "path=%s mountpoint=%.*s", path, (int)mountpoint_len, req->url.path);
             if(lws_http_transaction_completed(wsi))
                return -1;
            }

          }*/
        }

        if(/*req->method != METHOD_POST &&*/ server->cb.http.ctx) {
          cb = &server->cb.http;

          JSValue val = server_exception(server, callback_emit_this(cb, session->ws_obj, 3, args));
          JS_FreeValue(ctx, val);
        }

        if(!(req->method != METHOD_GET && wsi_http2(wsi))) {

          if((ret = http_server_respond(wsi, &b, resp, ctx, session))) {
            JS_FreeValue(ctx, session->ws_obj);
            session->ws_obj = JS_NULL;
            return 1;
          }
        }
      }

      goto http_exit;

      /*      LOGCB("HTTP", "NOT FOUND\tpath=%s mountpoint=%.*s", path, (int)mountpoint_len, req->url.path);
            if(cb && cb->ctx)
              server_exception(server, callback_emit(cb, 2, &session->req_obj));*/

    http_exit:
      if(!(req->method != METHOD_GET && wsi_http2(wsi)))
        if(req->method == METHOD_GET || wsi_http2(wsi))
          lws_callback_on_writable(wsi);

      /*JS_FreeValue(ctx, session->ws_obj);
      session->ws_obj=JS_NULL;*/
      return 0;
    }

    case LWS_CALLBACK_HTTP_WRITEABLE: {

      if(opaque->upstream)
        return lws_callback_http_dummy(wsi, reason, user, in, len);

      if(session->in_body)
        break;

      MinnetResponse* resp;
      BOOL done = FALSE;

      LOGCB("HTTP", "%smnt=%s", session->h2 ? "h2, " : "", session->mount ? session->mount->mnt : 0);
      if(!(resp = opaque->resp)) {
        resp = opaque->resp = response_new(ctx);
        session->resp_obj = minnet_response_wrap(ctx, resp);
      }

      ret = http_server_generate(ctx, session, resp, &done);

      if(resp->body)
        if(http_server_writable(wsi, resp, done) == 1)
          return http_server_callback(wsi, LWS_CALLBACK_HTTP_FILE_COMPLETION, session, in, len);

      return ret;
    }

    case LWS_CALLBACK_HTTP_DROP_PROTOCOL: {
      opaque_clear(opaque, ctx);
      break;
    }

    case LWS_CALLBACK_HTTP_FILE_COMPLETION: {
      opaque_clear(opaque, ctx);

      return lws_callback_http_dummy(wsi, reason, user, in, len);
      break;
    }

    case LWS_CALLBACK_CLOSED_HTTP: {
      LOGCB("HTTP(1)", "in='%.*s' url=%s session=%p", (int)len, (char*)in, opaque->req ? url_string(&opaque->req->url) : 0, session);
      // lws_close_free_wsi(wsi, LWS_CLOSE_STATUS_NOSTATUS, __func__);

      /*      if(opaque)
              opaque_free(opaque, ctx);

            if(session)
              session_clear(session, ctx);*/
      ret = -1;
      break;
    }
    case LWS_CALLBACK_VHOST_CERT_AGING:
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
    case LWS_CALLBACK_GET_THREAD_ID: {
      break;
    }
    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: /*{
      int ret;
      static uint8_t buffer[1024 + LWS_PRE];
      ByteBuffer buf = BUFFER(buffer);
      int len = buffer_AVAIL(&buf);

      ret = lws_http_client_read(wsi, (char**)&buf.write, &len);
      if(ret)
        return -1;
      return 0;
      break;
    }*/
    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {
      struct wsi_opaque_user_data* opaque2 = lws_get_opaque_user_data(lws_get_parent(wsi));

      opaque2->upstream = wsi;
    }
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
    case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: {
      return lws_callback_http_dummy(wsi, reason, user, in, len);
    }
    default: {
      minnet_lws_unhandled(__func__, reason);
      break;
    }
  }
  // int ret = 0;
  if(reason != LWS_CALLBACK_HTTP_WRITEABLE && (reason < LWS_CALLBACK_HTTP_BIND_PROTOCOL || reason > LWS_CALLBACK_CHECK_ACCESS_RIGHTS)) {
    LOGCB("HTTP", "fd=%i %s%sin='%.*s' ret=%d\n", lws_get_socket_fd(wsi), (session && session->h2) || wsi_http2(wsi) ? "h2, " : "", wsi_tls(wsi) ? "ssl, " : "", (int)len, (char*)in, ret);
  }

  if(ret == 0)
    ret = lws_callback_http_dummy(wsi, reason, user, in, len);

  return ret;
}

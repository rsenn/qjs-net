#include "minnet.h"
#include "minnet-server-http.h"
#include <assert.h>
#include <ctype.h>
#include <cutils.h>
#include <inttypes.h>
#include <libwebsockets.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "context.h"
#include "headers.h"
#include "jsutils.h"
#include "minnet-form-parser.h"
#include "minnet-generator.h"
#include "minnet-request.h"
#include "minnet-response.h"
#include "minnet-server.h"
#include "minnet-url.h"
#include "minnet-websocket.h"
#include "opaque.h"
#include <quickjs.h>
#include "utils.h"
#include "buffer.h"

static int serve_generator(JSContext* ctx, struct session_data* session, struct lws* wsi, BOOL* done_p);

int lws_hdr_simple_create(struct lws*, enum lws_token_indexes, const char*);

MinnetVhostOptions*
vhost_options_create(JSContext* ctx, const char* name, const char* value) {
  MinnetVhostOptions* vo = js_mallocz(ctx, sizeof(MinnetVhostOptions));

  DBG("name=%s value=%s", name, value);

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
    DBG("option=#%u name=%s value=%s", i, vo->name, vo->value);

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

/*void
vhost_options_free(JSContext* ctx, MinnetVhostOptions* vo) {

  if(vo->name)
    js_free(ctx, (void*)vo->name);
  if(vo->value)
    js_free(ctx, (void*)vo->value);

  if(vo->options)
    vhost_options_free_list(ctx, vo->options);

  js_free(ctx, (void*)vo);
}
*/
MinnetHttpMount*
mount_create(JSContext* ctx, const char* mountpoint, const char* origin, const char* def, const char* pro, enum lws_mount_protocols origin_proto) {
  MinnetHttpMount* m;

  if((m = js_mallocz(ctx, sizeof(MinnetHttpMount)))) {
    DBG("mountpoint=%s origin=%s default=%s protocol=%-10s origin_protocol=%s",
        mountpoint,
        origin,
        def,
        pro,
        ((const char*[]){"HTTP", "HTTPS", "FILE", "CGI", "REDIR_HTTP", "REDIR_HTTPS", "CALLBACK"})[origin_proto]);

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
  const char* path;

  if(JS_IsArray(ctx, obj)) {
    int i = 0;
    if(!key)
      mnt = JS_GetPropertyUint32(ctx, obj, i++);
    org = JS_GetPropertyUint32(ctx, obj, i++);
    def = JS_GetPropertyUint32(ctx, obj, i++);
    pro = JS_GetPropertyUint32(ctx, obj, i++);

  } else if(JS_IsFunction(ctx, obj)) {
    if(!key)
      mnt = js_function_name_value(ctx, obj);
    org = JS_DupValue(ctx, obj);
  }

  if(key)
    mnt = JS_NewString(ctx, key);

  {
    size_t namelen;
    const char* namestr = JS_ToCStringLen(ctx, &namelen, mnt);
    char buf[namelen + 2];
    size_t i = namestr[0] == '/' ? 0 : 1;

    pstrcpy(&buf[i], namelen + 1, namestr);
    buf[0] = '/';
    buf[namelen + i] = '\0';
    mnt = JS_NewString(ctx, buf);
  }

  path = JS_ToCString(ctx, mnt);

  DBG("key=%s path='%s'", key, path);

  if(JS_IsFunction(ctx, org)) {
    ret = mount_create(ctx, path, 0, 0, 0, LWSMPRO_CALLBACK);

    GETCBTHIS(org, ret->callback, JS_UNDEFINED);

  } else {
    const char* dest = JS_ToCString(ctx, org);
    char* protocol = js_is_nullish(pro) ? 0 : js_tostring(ctx, pro);
    const char* dotslashslash = strstr(dest, "://");
    size_t plen = dotslashslash ? dotslashslash - dest : 0;
    const char* origin = &dest[plen ? plen + 3 : 0];
    const char* index = js_is_nullish(def) ? 0 : JS_ToCString(ctx, def);
    enum lws_mount_protocols proto = plen == 0 ? LWSMPRO_FILE : (plen == 5 && !strncmp(dest, "https", plen)) ? LWSMPRO_HTTPS : LWSMPRO_HTTP;

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

MinnetHttpMount*
mount_find(MinnetHttpMount* mounts, const char* x, size_t n) {
  struct lws_http_mount *p, *m = 0;
  int protocol = n == 0 ? LWSMPRO_CALLBACK : LWSMPRO_HTTP;
  size_t l = 0;

  DBG("x='%.*s'", (int)n, x);

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
      DBG("x='%.*s' '%.*s'", (int)n, x, (int)len, mnt);

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
    DBG("org=%s mnt=%s cb.ctx=%p", ((MinnetHttpMount*)m)->org, ((MinnetHttpMount*)m)->mnt, ((MinnetHttpMount*)m)->callback.ctx);
  }
  return (MinnetHttpMount*)m;
}

MinnetHttpMount*
mount_find_s(MinnetHttpMount* mounts, const char* x) {
  struct lws_http_mount *p, *m = 0;
  size_t n = strlen(x);

  for(p = (struct lws_http_mount*)mounts; p; p = (struct lws_http_mount*)p->mount_next) {
    const char* mnt = p->mountpoint;
    size_t len = p->mountpoint_len;

    DBG("x='%.*s' '%.*s'", (int)n, x, (int)len, mnt);

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
  return (MinnetHttpMount*)m;
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

typedef struct {
  int ref_count;
  JSContext* ctx;
  struct session_data* session;
  MinnetResponse* resp;
  struct lws* wsi;
} HTTPAsyncResolveClosure;

static void
serve_resolved_free(void* ptr) {
  HTTPAsyncResolveClosure* closure = ptr;

  if(--closure->ref_count == 0) {

    if(closure->session) {
      if(closure->session->wait_resolve_ptr == &closure->session)
        closure->session->wait_resolve_ptr = 0;
    }

    if(closure->resp)
      response_free(closure->resp, JS_GetRuntime(closure->ctx));

    js_free(closure->ctx, ptr);
  }
}

static JSValue
serve_rejected(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  HTTPAsyncResolveClosure* closure = ptr;
  struct session_data* session;

  if((session = closure->session)) {

    const char* message = JS_ToCString(ctx, argv[0]);

    assert(session->wait_resolve > 0);
    --session->wait_resolve;

    DBG("wait_resolve=%i error=%s", session->wait_resolve, message);

    MinnetServer* server;

    if((server = lws_context_user(lws_get_context(closure->wsi))))
      server_exception(server, JS_Throw(ctx, argv[0]));

    queue_write(&session->sendq, message, strlen(message), ctx);
    queue_close(&session->sendq);

    JS_FreeCString(ctx, message);
  }
  return JS_UNDEFINED;
}

static JSValue
serve_resolved(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  HTTPAsyncResolveClosure* closure = ptr;
  struct session_data* session;

  if((session = closure->session)) {
    assert(session->wait_resolve > 0);
    --session->wait_resolve;

    if(JS_IsObject(argv[0])) {
      JSValue value = JS_GetPropertyStr(ctx, argv[0], "value");
      JSValue done_prop = JS_GetPropertyStr(ctx, argv[0], "done");
      BOOL done = JS_ToBool(ctx, done_prop);
      JSBuffer out = js_buffer_new(ctx, value);
      JS_FreeValue(ctx, done_prop);

      DBG("wait_resolve=%i value=%s done=%i out={ size: %zu, data: '%.*s' }", session->wait_resolve, JS_ToCString(ctx, value), done, out.size, out.size > 50 ? 50 : (int)out.size, out.data);

      if(out.data) {
        queue_write(&session->sendq, out.data, out.size, ctx);

        session_want_write(session, closure->wsi);
      }

      js_buffer_free(&out, ctx);

      JS_FreeValue(ctx, value);

      if(done) {
        queue_close(&session->sendq);
      } /* else {
         int ret = serve_generator(ctx, session, closure->wsi, &done);

      DBG("done=%i ret=%i wait_resolve=%i", done, ret, session->wait_resolve);

         assert(session->wait_resolve);
       }*/
    }
  }

  return JS_UNDEFINED;
}

static JSValue
serve_promise(JSContext* ctx, struct session_data* session, JSValueConst value) {
  struct wsi_opaque_user_data* opaque = session_opaque(session);
  HTTPAsyncResolveClosure* p;
  JSValue ret = JS_UNDEFINED;

  ++session->wait_resolve;

  DBG("promise=%s", JS_ToCString(ctx, value));

  if((p = js_malloc(ctx, sizeof(HTTPAsyncResolveClosure)))) {
    *p = (HTTPAsyncResolveClosure){2, ctx, session, opaque->resp ? response_dup(opaque->resp) : 0, session_wsi(session)};

    session->wait_resolve_ptr = &p->session;

    JSValue resolve = js_function_cclosure(ctx, serve_resolved, 1, 0, p, serve_resolved_free);
    JSValue thened = js_async_then(ctx, value, resolve);
    JSValue reject = js_function_cclosure(ctx, serve_rejected, 1, 0, p, serve_resolved_free);
    JSValue catched = js_async_catch(ctx, thened, reject);

    DBG("thened=%s catched=%s", JS_ToCString(ctx, thened), JS_ToCString(ctx, catched));

    JS_FreeValue(ctx, resolve);
    JS_FreeValue(ctx, reject);
    JS_FreeValue(ctx, thened);
    ret = catched;

  } else {
    ret = JS_EXCEPTION;
  }

  return ret;
}

static int
serve_generator(JSContext* ctx, struct session_data* session, struct lws* wsi, BOOL* done_p) {
  MinnetResponse* resp = minnet_response_data(session->resp_obj);

  /* if(!resp->body)
     response_generator(resp, ctx);*/

  DBG("callback=%" PRIu32 " run=%" PRIu32 " done=%s wait_resolve=%s closed=%s complete=%s",
      session->callback_count,
      ++session->generator_run,
      *done_p ? "TRUE" : "FALSE",
      session->wait_resolve ? "TRUE" : "FALSE",
      queue_closed(&session->sendq) ? "TRUE" : "FALSE",
      queue_complete(&session->sendq) ? "TRUE" : "FALSE");

  assert(session->wait_resolve == 0);
  assert(!queue_complete(&session->sendq));

  if(session->wait_resolve)
    return 0;

  if(JS_IsObject(session->generator) && !queue_complete(&session->sendq)) {
    session->next = JS_UNDEFINED;

    while(!*done_p && !session->wait_resolve) {
      JSValue ret = js_iterator_next(ctx, session->generator, &session->next, done_p, 0, 0);

      DBG("done=%s wait_resolve=%s ret=%s", *done_p ? "TRUE" : "FALSE", session->wait_resolve ? "TRUE" : "FALSE", JS_ToCString(ctx, ret));

      if(js_is_promise(ctx, ret)) {
        JSValue promise = serve_promise(ctx, session, ret);
        const char* prstr;
        DBG("pr=%s", prstr = JS_ToCString(ctx, promise));
        JS_FreeCString(ctx, prstr);
        JS_FreeValue(ctx, promise);
      } else if(JS_IsException(ret)) {
        JSValue exception = JS_GetException(ctx);
        js_error_print(ctx, exception);
        *done_p = TRUE;
      } else {
        JSBuffer out = JS_BUFFER_DEFAULT();
        if(js_buffer_from(ctx, &out, ret)) {
          DBG("out={ .data = '%.*s', .size = %zu }", (int)(out.size > 255 ? 255 : out.size), out.size > 255 ? &out.data[out.size - 255] : out.data, out.size);
          queue_write(&session->sendq, out.data, out.size, ctx);
        }
        js_buffer_free(&out, ctx);
      }
      JS_FreeValue(ctx, ret);
      if(*done_p)
        queue_close(&session->sendq);

      break; /* XXX: generate multiple? */
    }
  } else {
    *done_p = TRUE;
  }

  DBG("wait_resolve=%d sendq=%zu done=%s", session->wait_resolve, queue_bytes(&session->sendq), *done_p ? "TRUE" : "FALSE");

  if(/*!queue_complete(&session->sendq) ||*/ queue_bytes(&session->sendq))
    session_want_write(session, wsi);

  if(session->wait_resolve)
    lws_set_timeout(wsi, PENDING_TIMEOUT_HTTP_CONTENT, 30);

  return 0;
}

static int
serve_callback(JSCallback* cb, struct session_data* session, struct lws* wsi) {
  JSValue body;
  enum { SYNC = 1, ASYNC = 2 } type;

  type = session_callback(session, cb, wsi_context(wsi));

  DBG("type=%s generator=%s", ((const char*[]){"NONE", "SYNC", "ASYNC"})[type], JS_ToCString(cb->ctx, session->generator));

  switch(type) {
    case ASYNC: {
      BOOL done = FALSE;
      if(serve_generator(cb->ctx, session, wsi, &done))
        return 1;
      break;
    }
    case SYNC: {
      session_want_write(session, wsi);
      break;
    }
    default: {
      return -1;
    }
  }

  return 0;
}

static BOOL
has_transfer_encoding(MinnetRequest* req, const char* enc) {
  const char* accept;
  size_t len, enclen = strlen(enc);

  if((accept = headers_getlen(&req->headers, &len, "accept-encoding", "\r\n", ":"))) {
    size_t toklen, pos;

    for(pos = 0; pos < len; (pos += toklen, pos += scan_charsetnskip(&accept[pos], ", ", len - pos))) {
      toklen = scan_noncharsetnskip(&accept[pos], ", ", len - pos);

      if(toklen == enclen && !strncmp(&accept[pos], enc, toklen))
        return TRUE;
    }
  }

  return FALSE;
}

static int
serve_response(struct lws* wsi, ByteBuffer* buf, MinnetResponse* resp, JSContext* ctx, struct session_data* session) {
  struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);
  lws_filepos_t content_len = LWS_ILLEGAL_HTTP_CONTENT_LEN;

  if(session->response_sent)
    return 0;

  /*  if(!resp->body)
      response_generator(resp, ctx);*/

  DBG("status=%d generator=%d", resp->status, resp->body != NULL);

  /*if(!wsi_http2(wsi)) {
    BOOL done = FALSE;
    serve_generator(ctx, session, wsi, &done);
  }*/
  if(queue_complete(&session->sendq))
    content_len = queue_bytes(&session->sendq);

  if(resp->status >= 300 && resp->status <= 399) {
    size_t len;
    char* loc;

    if((loc = headers_getlen(&resp->headers, &len, "location", "\r\n", ":")))

      if(lws_http_redirect(wsi, resp->status, loc, len, &buf->write, buf->end))
        return 1;

    // headers_unset(&resp->headers, "location");

  } else {
    if(lws_add_http_common_headers(wsi, resp->status, 0, content_len, &buf->write, buf->end))
      return 1;
  }

  // headers_write(&resp->headers, wsi, &buf->write, buf->end);

  for(const uint8_t *x = resp->headers.start, *end = resp->headers.write; x < end; x += headers_next(x, end, "\r\n")) {
    size_t len, n;
    len = headers_length(x, end, "\r\n");
    n = headers_namelen(x, end);

    if(n == 8 && !strncasecmp(x, "location", n))
      continue;

    if(len > n) {
      char* prop = headers_name(x, end, ctx);
      n = headers_value(x, end, ":");

      DBG("header=%s = value='%.*s'", prop, (int)(len - n), &x[n]);
      if((lws_add_http_header_by_name(wsi, (const unsigned char*)prop, (const unsigned char*)&x[n], len - n, &buf->write, buf->end)))
        JS_ThrowInternalError(ctx, "Adding header '%s' failed", prop);
      js_free(ctx, (void*)prop);
    }
  }

  if(has_transfer_encoding(opaque->req, "deflate")) {
    if(!(byte_finds(buf->start, block_SIZE(buf), wsi_http2(wsi) ? "\020content-encoding" : "content-encoding") < block_SIZE(buf)))
      lws_http_compression_apply(wsi, "deflate", &buf->write, buf->end, 0);
  }

  int ret = lws_finalize_write_http_header(wsi, buf->start, &buf->write, buf->end);

  DBG("headers='%.*s'", (int)buffer_HEAD(buf), buf->start);

  if(ret)
    return 2;

  session->response_sent = TRUE;

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
serve_file(JSContext* ctx, struct session_data* session, struct lws* wsi, const char* path, MinnetHttpMount* mount) {
  MinnetResponse* resp = opaque_fromwsi(wsi)->resp;
  FILE* fp;
  const char* mime = lws_get_mimetype(path, &mount->lws);
  // BOOL compressed = has_transfer_encoding(req, "gzip");

  DBG("path=%s mount=%s mime=%s", path, mount->mnt, mime);

  if(path[0] == '\0')
    path = mount->def;

  response_generator(resp, ctx);

  if((fp = fopen(path, "rb"))) {
    size_t n = file_size(fp);
    ByteBlock blk;

    block_alloc(&blk, n);
    if(fread(blk.start, n, 1, fp) == 1) {
      queue_put(&session->sendq, blk, ctx);
      queue_close(&session->sendq);
    } else {
      block_free(&blk);
    }

    if(mime)
      response_settype(resp, mime);

    fclose(fp);

  } else {
    const char* body = "<html>\n  <head>\n    <title>404 Not Found</title>\n    <meta charset=utf-8 http-equiv=\"Content-Language\" content=\"en\"/>\n  </head>\n  <body>\n    <h1>404 Not "
                       "Found</h1>\n  </body>\n</html>\n";
    resp->status = 404;

    // response_write(resp, body, strlen(body), ctx);
    queue_write(&session->sendq, body, strlen(body), ctx);
    queue_close(&session->sendq);
  }

  session_want_write(session, wsi);

  lwsl_user("serve_file path=%s mount=%.*s gen=%d mime=%s", path, mount->lws.mountpoint_len, mount->lws.mountpoint, resp->body != NULL, mime);

  return 0;
}

static int
http_server_writeable(struct session_data* session, struct lws* wsi, BOOL done) {
  struct http_response* resp = minnet_response_data(session->resp_obj);
  enum lws_write_protocol n, wp = -1;
  size_t remain = 0;
  ssize_t ret = 0;
  size_t qsize = queue_bytes(&session->sendq);

  DBG("callback=%" PRIu32 " generator=%d qsize=%zu done=%d", session->callback_count, resp->body != NULL, qsize, done);

  n = done ? LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP;

  if(qsize) {
    ByteBlock buf;
    size_t pos = 0;

    buf = queue_next(&session->sendq, &done);

    while((remain = block_SIZE(&buf) - pos) > 0) {

      uint8_t* x = block_BEGIN(&buf) + pos;
      size_t l = wsi_http2(wsi) ? (remain > 1024 ? 1024 : remain) : remain;

      if(l > 0) {
        wp = queue_complete(&session->sendq) && remain == l ? LWS_WRITE_HTTP_FINAL : n;
        ret = lws_write(wsi, x, l, wp);
        DBG("len=%zu final=%d ret=%zd data='%.*s'", l, wp == LWS_WRITE_HTTP_FINAL, ret, (int)(l > 32 ? 32 : l), x);

        remain -= l;
        pos += l;
      }
    }

    block_free(&buf);
  }
  DBG("done=%i remain=%zu closed=%d", done, remain, queue_closed(&session->sendq));

  if(done || queue_closed(&session->sendq))
    return 1;

  /* if(!done) session_want_write(session, wsi);*/

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
    return wsi_handle_poll(wsi, reason, &server->on.fd, in);
  }

  if(!opaque && ctx)
    opaque = lws_opaque(wsi, ctx);

  assert(opaque);

  if(session) {

    if(session->callback)
      return session->callback(wsi, reason, user, in, len);

    if(opaque)
      opaque->sess = session;
    ++session->callback_count;
  }

  if(reason != LWS_CALLBACK_HTTP_WRITEABLE)
    LOGCB("HTTP(1)",
          "callback=%" PRId32 " %s%sfd=%d len=%d in='%.*s' url=%s",
          session ? session->callback_count : -1,
          wsi_http2(wsi) ? "h2, " : "http/1.1, ",
          wsi_tls(wsi) ? "TLS, " : "plain, ",
          lws_get_socket_fd(lws_get_network_wsi(wsi)),
          (int)len,
          (int)MIN(32, len),
          (char*)in,
          opaque && opaque->req ? url_string(&opaque->req->url) : 0);

  if(opaque->upstream)
    if(reason != LWS_CALLBACK_FILTER_HTTP_CONNECTION)
      return lws_callback_http_dummy(wsi, reason, user, in, len);

  switch(reason) {
    case LWS_CALLBACK_PROTOCOL_INIT: {
      session_zero(session);
      return 0;
    }
    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CHECK_ACCESS_RIGHTS:
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS:
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
    case LWS_CALLBACK_PROTOCOL_DESTROY:
    case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: break;

    case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {
      if((session->mount = mount_find((MinnetHttpMount*)server->context.info.mounts, in, len)))
        if(mount_is_proxy(session->mount))
          lws_hdr_simple_create(wsi, wsi_http2(wsi) ? WSI_TOKEN_HTTP_COLON_AUTHORITY : WSI_TOKEN_HOST, "");

      if(opaque->upstream)
        return lws_callback_http_dummy(wsi, reason, user, in, len);

      if(ctx && opaque->ws)
        session->ws_obj = minnet_ws_wrap(ctx, opaque->ws);
      if(!opaque->req)
        opaque->req = request_fromwsi(wsi, ctx);
      if(in) {
        /* opaque->uri = in;
         opaque->uri_len = len ? len : strlen(in);*/
        url_set_path_len(&opaque->req->url, in, len, ctx);
      }
      url_set_protocol(&opaque->req->url, wsi_tls(wsi) ? "https" : "http");
      break;
    }

    case LWS_CALLBACK_HTTP_BIND_PROTOCOL: {
      opaque->status = OPEN;
      if(opaque->req)
        url_set_protocol(&opaque->req->url, wsi_tls(wsi) ? "https" : "http");
      break;
    }

    case LWS_CALLBACK_ADD_HEADERS: {
      break;
    }

    case LWS_CALLBACK_HTTP_BODY: {
      // LOGCB("HTTP", "%slen: %zu parser: %p", wsi_http2(wsi) ? "h2, " : "", len, opaque->form_parser);

      MinnetRequest* req = minnet_request_data2(ctx, session->req_obj);
      session->in_body = TRUE;

      if(len) {
        if(opaque->form_parser) {
          form_parser_process(opaque->form_parser, in, len);
        } else {
          if(!req->body)
            req->body = generator_new(ctx);

          if(!req->body->q || !req->body->q->continuous)
            generator_continuous(req->body, JS_NULL);

          generator_write(req->body, in, len, JS_UNDEFINED);
        }
      }

      if(server->on.read.ctx) {
        JSValue args[] = {
            JS_NewStringLen(server->on.read.ctx, in, len),
        };
        JSValue ret = server_exception(server, callback_emit_this(&server->on.read, session->req_obj, countof(args), args));
        JS_FreeValue(server->on.read.ctx, ret);
      }
      break;
    }

    case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
      MinnetFormParser* fp;
      ByteBuffer b = BUFFER(buf);
      JSCallback* cb;
      MinnetRequest* req = opaque->req;
      Generator* gen = req->body;

      session->in_body = FALSE;

      LOGCB("HTTP(2)", "%slen: %zu", wsi_http2(wsi) ? "h2, " : "", len);

      if((fp = opaque->form_parser)) {
        lws_spa_finalize(fp->spa);
        if(fp->cb.finalize.ctx) {
          JSValue ret = server_exception(server, callback_emit(&fp->cb.finalize, 0, 0));
          JS_FreeValue(fp->cb.finalize.ctx, ret);
        }
      }

      cb = session->mount ? &session->mount->callback : 0;

      if(cb && cb->ctx) {
        assert(!JS_IsObject(session->generator));
        ret = serve_callback(cb, session, wsi);
        /*    JSValue ret = server_exception(server, callback_emit_this(cb, session->ws_obj, 2, session->args));

            assert(js_is_iterator(ctx, ret));
            session->generator = ret;*/
      }

      if(gen) {
        DBG("gen=%p", gen);
        BOOL done = FALSE;
        JSValue value = generator_dequeue(gen, &done);

        js_async_resolve(ctx, &session->async, value);
        JS_FreeValue(ctx, value);

        generator_close(req->body, JS_UNDEFINED);
      }

      // js_async_resolve(ctx, &session->async,

      if(server->on.post.ctx) {
        JSValue args[] = {
            minnet_generator_iterator(server->on.post.ctx, gen),
        };
        JSValue ret = server_exception(server, callback_emit_this(&server->on.post, session->req_obj, countof(args), args));
        JS_FreeValue(server->on.post.ctx, ret);

      } else {
      }
      if(serve_response(wsi, &b, opaque->resp, ctx, session)) {
        JS_FreeValue(ctx, session->ws_obj);
        session->ws_obj = JS_NULL;
        // return 1;
      }

      session_want_write(session, wsi);
      break;
    }

    case LWS_CALLBACK_HTTP: {
      MinnetRequest* req = opaque->req ? opaque->req : (opaque->req = request_fromwsi(wsi, ctx));
      char* path = in;
      size_t mountpoint_len = 0, pathlen = 0;
      MinnetHttpMount *mounts, *mount;
      JSCallback* cb;

      assert(req);
      assert(req->url.path);

      pathlen = req->url.path ? strlen(req->url.path) : 0;

      /*if(opaque->uri)
        mountpoint_len = (char*)in - opaque->uri;
      else */
      if(req->url.path && in && len < pathlen)
        mountpoint_len = pathlen - len;

      LOGCB("HTTP(2)", "mountpoint='%.*s' path='%s'", (int)mountpoint_len, req->url.path, path);

      if(!opaque->req->headers.write)
        headers_tobuffer(ctx, &opaque->req->headers, wsi);

      mounts = (MinnetHttpMount*)server->context.info.mounts;

      if(!session->mount)
        if(path)
          session->mount = mount_find(mounts, path, 0);
      if(!session->mount)
        if(req->url.path)
          session->mount = mount_find(mounts, req->url.path, mountpoint_len);
      if(req->url.path && !session->mount)
        if(!(session->mount = mount_find(mounts, req->url.path, mountpoint_len)))
          session->mount = mount_find(mounts, req->url.path, 0);

      if((mount = session->mount)) {
        size_t mlen = strlen(mount->mnt);

        DBG("mnt='%s'", mount->mnt);
        DBG("mnt='%.*s'", (int)mlen, mount->mnt);

        assert(req->url.path);
        assert(mount->mnt);
        assert(mlen);

        if(!strcmp(req->url.path + mlen, path)) {
          assert(!strcmp(req->url.path + mlen, path));

          LOGCB("HTTP(2)",
                "mount: mnt='%s', org='%s', pro='%s', origin_protocol='%s'\n",
                mount->mnt,
                mount->org,
                mount->pro,
                ((const char*[]){
                    "HTTP",
                    "HTTPS",
                    "FILE",
                    "CGI",
                    "REDIR_HTTP",
                    "REDIR_HTTPS",
                    "CALLBACK",
                })[(uintptr_t)mount->lws.origin_protocol]);
        }

        session->req_obj = minnet_request_wrap(ctx, opaque->req);

        if(!JS_IsObject(session->ws_obj))
          if(opaque->ws)
            session->ws_obj = minnet_ws_wrap(ctx, opaque->ws);

        if(!JS_IsObject(session->resp_obj))
          session->resp_obj = minnet_response_new(ctx, req->url, 200, 0, TRUE, "text/html");

        Response* resp = opaque->resp = minnet_response_data2(ctx, session->resp_obj);
        LOGCB("HTTP(3)", "req=%p, header=%zu mnt=%s org=%s", req, buffer_HEAD(&req->headers), mount->mnt, mount->org);
        request_dup(req);
        cb = &mount->callback;
        if(mount && !mount->callback.ctx)
          cb = 0;

        if(mount && mount->lws.origin_protocol == LWSMPRO_FILE) {
          if(*path == '\0' && mount->def) {
            response_redirect(resp, HTTP_STATUS_MOVED_PERMANENTLY, mount->def);
            session_want_write(session, wsi);
            lws_set_timeout(wsi, PENDING_TIMEOUT_USER_REASON_BASE, 30);
          } else {
            mount = mount_find_s(mounts, "/404.html");

            cb = &mount->callback;
          }
          /*  session_want_write(session, wsi);
            opaque->resp->status = HTTP_STATUS_NOT_FOUND;
            ret = 0;
            return ret;*/
        }

        /*if(mount && ((mount->lws.origin_protocol == LWSMPRO_CALLBACK && (!cb || !cb->ctx)) ||
                     (mount->lws.origin_protocol == LWSMPRO_FILE || (mount->lws.origin_protocol == LWSMPRO_CALLBACK && mount->lws.origin)))) {
          ret = serve_file(ctx, session, wsi, path, mount);

          if(ret) {
            LOGCB("HTTP(4)", "serve_file FAIL %d", ret);
            JS_FreeValue(ctx, session->ws_obj);
            session->ws_obj = JS_NULL;
            session_want_write(session, wsi);
            ret = 0;
            break;
          }
        }*/

        if(mount && mount->lws.origin_protocol == LWSMPRO_CALLBACK) {
          if(cb && cb->ctx) {
            if(req->method == METHOD_GET) {
              /*    resp = session_callback(resp, session, cb);
                  JSValue gen = callback_emit_this(cb, session->ws_obj, 2, &args[1]);
                  gen = server_exception(server, gen);

                  LOGCB("HTTP(5)",
                        "gen=%s next=%s is_iterator=%d is_async_generator=%d",
                        JS_ToCString(ctx, gen),
                        JS_ToCString(ctx, JS_GetPropertyStr(ctx, gen, "next")),
                        js_is_iterator(ctx, gen),
                        js_is_async_generator(ctx, gen));
                  if(js_is_iterator(ctx, gen)) {
                    assert(js_is_iterator(ctx, gen));
                    session->generator = gen;
                    session->next = JS_UNDEFINED;

                    if(js_is_async_generator(ctx, gen)) {
                      BOOL done = FALSE;
                      ret = serve_generator(ctx, session,  wsi, &done);
                    } else*/
              /*session_want_write(session, wsi);
          } else {
            LOGCB("HTTP(6)", "gen=%s", JS_ToCString(ctx, gen));
          }*/
              ret = serve_callback(cb, session, wsi);
            }
          }
        }
      }
      //_O_ if(req->method == METHOD_POST)

      request_promise(req, &session->async, ctx);

      if(callback_valid(&server->on.http)) {
        cb = &server->on.http;
        JSValue val = server_exception(server, callback_emit_this(cb, session->ws_obj, 2, &session->req_obj));
        JS_FreeValue(ctx, val);
      }

      return ret;
      break;
    }

    case LWS_CALLBACK_HTTP_WRITEABLE: {
      MinnetResponse* resp;
      BOOL done = FALSE;
      uint32_t qsize;

      if(!session->want_write)
        return 0;

      assert(session->want_write);
      session->want_write = FALSE;

      LOGCB("HTTP(2)",
            "callback=%" PRIu32 " %smnt=%s closed=%d complete=%d sendq=%zu wait_resolve=%d",
            session->callback_count,
            wsi_http2(wsi) ? "h2, " : "",
            session->mount ? session->mount->mnt : 0,
            queue_closed(&session->sendq),
            queue_complete(&session->sendq),
            queue_bytes(&session->sendq),
            session->wait_resolve);

      if(!(resp = opaque->resp)) {
        resp = opaque->resp = response_new(ctx);
        session->resp_obj = minnet_response_wrap(ctx, resp);
      }

      if(!session->response_sent) {
        ByteBuffer b = BUFFER(buf);
        session->response_sent = !serve_response(wsi, &b, opaque->resp, ctx, session);
      }

      if(!(qsize = queue_bytes(&session->sendq))) {
        if(!(queue_closed(&session->sendq) || queue_complete(&session->sendq)) && !session->wait_resolve)
          ret = serve_generator(ctx, session, wsi, &done);
      }

      // if(queue_closed(&session->sendq) || queue_size(&session->sendq))
      if(http_server_writeable(session, wsi, !!queue_closed(&session->sendq)))
        ret = http_server_callback(wsi, LWS_CALLBACK_HTTP_FILE_COMPLETION, session, in, len);
      else if(queue_closed(&session->sendq) && queue_complete(&session->sendq))
        ret = lws_http_transaction_completed(wsi);

      if(qsize && !session->want_write) {
        if(!(queue_closed(&session->sendq) || queue_complete(&session->sendq)) && !session->wait_resolve)
          ret = serve_generator(ctx, session, wsi, &done);
      }

      LOGCB("HTTP(3)", "callback=%" PRIu32 " ret=%d sendq=%zu want_write=%d wait_resolve=%d", session->callback_count, ret, queue_bytes(&session->sendq), session->want_write, session->wait_resolve);

      return ret;

      break;
    }

    case LWS_CALLBACK_HTTP_DROP_PROTOCOL: {
      // opaque_clear(opaque, ctx);
      break;
    }

    case LWS_CALLBACK_HTTP_FILE_COMPLETION: {
      // opaque_clear(opaque, ctx);

      return lws_callback_http_dummy(wsi, reason, user, in, len);
      break;
    }

    case LWS_CALLBACK_CLOSED_HTTP: {
      /* if(session->wait_resolve_ptr) {
         *session->wait_resolve_ptr = 0;
         session->wait_resolve_ptr = 0;
       }*/
      session_clear(session, JS_GetRuntime(ctx));

      // lws_close_free_wsi(wsi, LWS_CLOSE_STATUS_NOSTATUS, __func__);
      ret = -1;
      break;
    }
    case LWS_CALLBACK_VHOST_CERT_AGING:
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
    case LWS_CALLBACK_GET_THREAD_ID: {
      break;
    }

    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {
      struct wsi_opaque_user_data* opaque2 = lws_get_opaque_user_data(lws_get_parent(wsi));

      opaque2->upstream = wsi;
    }
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP:
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
  if(/*reason != LWS_CALLBACK_HTTP_WRITEABLE && reason != LWS_CALLBACK_CLOSED_HTTP &&*/ (reason < LWS_CALLBACK_HTTP_BIND_PROTOCOL || reason > LWS_CALLBACK_CHECK_ACCESS_RIGHTS)) {
    LOGCB("SERVER-HTTP(/)",
          "%s%sfd=%i ret=%d want_write=%d\n",
          wsi_http2(wsi) ? "h2, " : "http/1.1, ",
          wsi_tls(wsi) ? "TLS, " : "plain, ",
          lws_get_socket_fd(lws_get_network_wsi(wsi)),
          ret,
          session ? session->want_write : 0);
  }

  if(ret == 0)
    ret = lws_callback_http_dummy(wsi, reason, user, in, len);

  return ret;
}

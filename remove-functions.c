__attribute__((visibility("default"))) JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_minnet_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "Response");
  JS_AddModuleExport(ctx, m, "Request");
  JS_AddModuleExport(ctx, m, "Ringbuffer");
  JS_AddModuleExport(ctx, m, "Generator");
  JS_AddModuleExport(ctx, m, "Socket");
  JS_AddModuleExport(ctx, m, "FormParser");
  JS_AddModuleExport(ctx, m, "Hash");
  JS_AddModuleExport(ctx, m, "URL");
  JS_AddModuleExport(ctx, m, "default");
  JS_AddModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));

  minnet_log_ctx = ctx;

  lws_set_log_level(minnet_log_level, minnet_log_callback);

  return m;
}

ByteBuffer
buffer_move(ByteBuffer* buf) {
  ByteBuffer ret = *buf;
  memset(buf, 0, sizeof(ByteBuffer));
  return ret;
}

size_t
byte_findb(const void* haystack, size_t hlen, const void* what, size_t wlen) {
  size_t i, last;
  const char* s = (const char*)haystack;
  if(hlen < wlen)
    return hlen;
  last = hlen - wlen;
  for(i = 0; i <= last; i++) {
    if(byte_equal(s, wlen, what))
      return i;
    s++;
  }
  return hlen;
}

DoubleWord
deferred_call_x(Deferred* def, ...) {
  ptr_t const* av = def->argv;
  DoubleWord ret = {{0, 0}};
  va_list a;
  int argc = def->argc;
  size_t arg;

  va_start(a, def);

  while(argc < countof(def->argv) && (arg = va_arg(a, size_t))) {
    if(arg == DEFERRED_SENTINEL)
      break;
    def->argv[argc++] = (ptr_t)arg;
  }

  va_end(a);

  assert(!def->only_once || def->num_calls < 1);

  if(!def->only_once || def->num_calls < 1) {

    if(def->func == (void*)&JS_Call)
      ret = def->func(av[0], av[1], av[2], av[3], av[4], (ptr_t)(size_t)((argc - def->argc) > 0 ? 1 : 0), (ptr_t)&av[def->argc], 0);
    else
      ret = def->func(av[0], av[1], av[2], av[3], av[4], av[5], av[6], av[7]);

    ++def->num_calls;
  }

  return ret;
}

Deferred*
deferred_dupjs(JSValueConst value, JSContext* ctx) {
  JSValue v = JS_DupValue(ctx, value);
  return deferred_newjs(v, ctx);
}

Deferred*
deferred_newv(ptr_t fn, int argc, ptr_t argv[]) {
  Deferred* def;

  if(!(def = malloc(sizeof(Deferred))))
    return 0;

  deferred_init(def, fn, argc, argv);

  def->ref_count = 1;
  def->num_calls = 0;
  def->only_once = FALSE;

  return def;
}

JSValue
deferred_tojs(Deferred* def, JSContext* ctx) {
  deferred_dup(def);

  return js_function_cclosure(ctx, deferred_js_call, 0, 0, def, (void (*)(ptr_t))deferred_free);
}

struct form_parser*
form_parser_new(JSContext* ctx, struct socket* ws, int nparams, const char* const* param_names, size_t chunk_size) {
  struct form_parser* fp;

  if((fp = form_parser_alloc(ctx)))
    form_parser_init(fp, ws, nparams, param_names, chunk_size);

  return fp;
}

char*
headers_atom(JSAtom atom, JSContext* ctx) {
  char* ret;
  const char* str = JS_AtomToCString(ctx, atom);
  size_t len = strlen(str);

  if((ret = js_malloc(ctx, len + 2))) {
    strcpy(ret, str);
    ret[len] = ':';
    ret[len + 1] = '\0';
  }
  return ret;
}

ssize_t
headers_set(JSContext* ctx, ByteBuffer* buffer, const char* name, const char* value) {
  size_t namelen = strlen(name), valuelen = strlen(value);
  size_t len = namelen + 2 + valuelen + 2;

  buffer_grow(buffer, len);
  buffer_write(buffer, name, namelen);
  buffer_write(buffer, ": ", 2);
  buffer_write(buffer, value, valuelen);
  buffer_write(buffer, "\r\n", 2);

  return len;
}

void
js_buffer_to(JSBuffer buf, void** pptr, size_t* plen) {
  if(pptr)
    *pptr = buf.data;
  if(plen)
    *plen = buf.size;
}

JSModuleDef*
js_module_find(JSContext* ctx, JSAtom name) {
  struct list_head *el, *list = js_module_list(ctx);

  list_for_each(el, list) {
    JSModuleDef* module = (void*)((char*)el - sizeof(JSAtom) * 2);

    if(((JSAtom*)module)[1] == name)
      return module;
  }
  return 0;
}

JSValue
minnet_form_parser_wrap(JSContext* ctx, MinnetFormParser* fp) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_form_parser_proto, minnet_form_parser_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, form_parser_dup(fp));

  return ret;
}

int
minnet_protocol_count(MinnetProtocols** plist) {
  int i;

  if(!*plist)
    return 0;

  for(i = 0;; i++) {
    if((*plist)[i].name == NULL)
      break;
  }
  return i;
}

static proxy_conn_t*
proxy_new() {
  proxy_conn_t* pc;
  if((pc = malloc(sizeof(*pc))))
    memset(pc, 0, sizeof(*pc));

  return pc;
}

int
proxy_ws_raw_msg_destroy(struct lws_dll2* d, void* user) {
  proxy_msg_t* msg = lws_container_of(d, proxy_msg_t, list);

  lws_dll2_remove(d);
  free(msg);

  return 0;
}

void
request_clear(Request* req, JSContext* ctx) {
  url_free(&req->url, ctx);
  buffer_free(&req->headers);
  if(req->ip) {
    free(req->ip);
    req->ip = 0;
  }
  if(req->body)
    generator_destroy(&req->body);
}

void
request_format(Request const* req, char* buf, size_t len, JSContext* ctx) {
  char* headers = buffer_escaped(&req->headers);
  char* url = url_format(req->url, ctx);
  snprintf(buf, len, FGC(196, "Request") " { method: '%s', url: '%s', headers: '%s' }", method_name(req->method), url, headers);

  js_free(ctx, headers);
  js_free(ctx, url);
}

void
response_format(const struct http_response* resp, char* buf, size_t len) {
  snprintf(buf, len, FGC(226, "Response") " { url.path: '%s', status: %d, headers_sent: %s, type: '%s' }", resp->url.path, resp->status, resp->headers_sent ? "true" : "false", resp->type);
}

struct url
url_create(const char* str, JSContext* ctx) {
  struct url ret = {1, 0, 0, 0, 0};
  url_parse(&ret, str, ctx);
  return ret;
}

static JSValue
want_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  WSWantWrite* closure = ptr;

  lws_callback_on_writable(closure->ws->lwsi);
  return JS_UNDEFINED;
}

static void
ws_want_write_free(void* ptr) {
  WSWantWrite* closure = ptr;
  JSContext* ctx = closure->ctx;

  ws_free(closure->ws, ctx);
  js_free(ctx, closure);
};

AsyncIterator*
asynciterator_new(JSContext* ctx) {
  AsyncIterator* it;

  if((it = js_malloc(ctx, sizeof(AsyncIterator)))) {
    asynciterator_zero(it);
  }

  return it;
}

void
block_init(ByteBlock* blk, uint8_t* start, size_t len) {
  blk->start = start;
  blk->end = blk->start + len;
}

ssize_t
block_concat(ByteBlock* blk, ByteBlock other) {
  if(block_append(blk, block_BEGIN(&other), block_SIZE(&other)) == -1)
    return -1;

  return block_SIZE(blk);
}

ByteBlock
block_new(size_t size) {
  ByteBlock ret = {0, 0};
  block_alloc(&ret, size);
  return ret;
}

ByteBlock
block_from(void* data, size_t size) {
  return (ByteBlock){data, (uint8_t*)data + size};
}

int
block_fromarraybuffer(ByteBlock* blk, JSValueConst value, JSContext* ctx) {
  size_t len;

  if(!(blk->start = JS_GetArrayBuffer(ctx, &len, value)))
    return -1;

  blk->end = blk->start + len;
  return 0;
}

void
buffer_init(ByteBuffer* buf, uint8_t* start, size_t len) {
  buf->start = start;
  buf->end = start + len;
  buf->read = buf->start;
  buf->write = buf->start;
  buf->alloc = 0;
}

int
buffer_fromarraybuffer(ByteBuffer* buf, JSValueConst value, JSContext* ctx) {
  int ret;

  if(!(ret = block_fromarraybuffer(&buf->block, value, ctx))) {
    buf->read = buf->start;
    buf->write = buf->start;
    buf->alloc = 0;
  }
  return ret;
}

void
buffer_finalizer(JSRuntime* rt, void* opaque, void* ptr) {
  // ByteBuffer* buf = opaque;
}

JSValue
buffer_toarraybuffer(ByteBuffer* buf, JSContext* ctx) {
  ByteBuffer moved = buffer_move(buf);
  return block_toarraybuffer(&moved.block, ctx);
}

JSValue
buffer_toarraybuffer_size(ByteBuffer* buf, size_t* sz, JSContext* ctx) {
  ByteBuffer moved = buffer_move(buf);
  if(sz)
    *sz = block_SIZE(&moved.block);
  return block_toarraybuffer(&moved.block, ctx);
}

void
buffer_dump(const char* n, ByteBuffer const* buf) {
  fprintf(stderr, "%s\t{ write = %td, read = %td, size = %td }\n", n, buf->write - buf->start, buf->read - buf->start, buf->end - buf->start);
  fflush(stderr);
}

uint8_t*
buffer_skip(ByteBuffer* buf, size_t size) {
  assert(buf->read + size <= buf->write);
  buf->read += size;
  return buf->read;
}

static void
deferred_freejs(Deferred* def) {
  JSValue value = deferred_getjs(def);

  JS_FreeValue(def->argv[0], value);
}

static void
deferred_freejs_rt(Deferred* def) {
  JSValue value = deferred_getjs(def);

  JS_FreeValueRT(def->argv[0], value);
}

Deferred*
deferred_newjs_rt(JSValue value, JSContext* ctx) {
  Deferred* def;

  if((def = deferred_new(fn, JS_GetRuntime(ctx), value)))
    def->next = deferred_new(deferred_freejs_rt, def);

  return def;
}

int
headers_addobj(ByteBuffer* buffer, struct lws* wsi, JSValueConst obj, JSContext* ctx) {
  JSPropertyEnum* tab;
  uint32_t tab_len, i;

  if(JS_GetOwnPropertyNames(ctx, &tab, &tab_len, obj, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK))
    return 0;

  for(i = 0; i < tab_len; i++) {
    JSValue value = JS_GetProperty(ctx, obj, tab[i].atom);
    size_t len;
    void* prop;
    const void* str;
    int ret;

    str = JS_ToCStringLen(ctx, &len, value);
    JS_FreeValue(ctx, value);

    prop = headers_atom(tab[i].atom, ctx);

    ret = lws_add_http_header_by_name(wsi, prop, str, len, &buffer->write, buffer->end);

    js_free(ctx, prop);
    JS_FreeCString(ctx, str);

    if(ret)
      return -1;
  }

  js_free(ctx, tab);
  return 0;
}

ssize_t
headers_copy(ByteBuffer* buffer, char* dest, size_t sz, const char* name) {
  char* hdr;
  size_t len;

  if((hdr = headers_getlen(buffer, &len, name, "\r\n", ":"))) {
    len = MIN(len, sz);

    strncpy(dest, hdr, len);
    return len;
  }

  return -1;
}

ssize_t
headers_unset(ByteBuffer* buffer, const char* name) {
  return headers_unsetb(buffer, name, strlen(name));
}

char*
headers_tostring(JSContext* ctx, struct lws* wsi) {
  ByteBuffer buf = BUFFER_0();
  char* ret = 0;
  headers_tobuffer(ctx, &buf, wsi);

  ret = js_strndup(ctx, (const char*)buf.start, buffer_BYTES(&buf));

  buffer_free(&buf);
  return ret;
}

BOOL
generator_cancel(Generator* gen) {
  BOOL ret = FALSE;

  if(!queue_complete(gen->q)) {
    queue_close(gen->q);
    ret = TRUE;
  }
  if(asynciterator_cancel(&gen->iterator, JS_UNDEFINED, gen->ctx))
    ret = TRUE;

  return ret;
}

JSValue
generator_stop(Generator* gen) {
  ResolveFunctions funcs = {JS_NULL, JS_NULL};
  JSValue ret = js_promise_create(gen->ctx, &funcs);

  if(!generator_close(gen, funcs.resolve)) {
    JS_FreeValue(gen->ctx, JS_Call(gen->ctx, funcs.reject, JS_UNDEFINED, 0, 0));
  }

  js_promise_free(gen->ctx, &funcs);
  return ret;
}

BOOL
js_promise_done(ResolveFunctions const* funcs) {
  return js_resolve_functions_is_null(funcs);
}

BOOL
js_get_propertystr_bool(JSContext* ctx, JSValueConst obj, const char* str) {
  BOOL ret = FALSE;
  JSValue value;
  value = JS_GetPropertyStr(ctx, obj, str);
  if(!JS_IsException(value))
    ret = JS_ToBool(ctx, value);

  JS_FreeValue(ctx, value);
  return ret;
}

JSModuleDef*
js_module_find_s(JSContext* ctx, const char* name) {
  JSAtom atom;
  JSModuleDef* module;
  atom = JS_NewAtom(ctx, name);
  module = js_module_find(ctx, atom);
  JS_FreeAtom(ctx, atom);
  return module;
}

void*
js_module_export_find(JSModuleDef* module, JSAtom name) {
  void* export_entries = *(void**)((char*)module + sizeof(int) * 2 + sizeof(struct list_head) + sizeof(void*) + sizeof(int) * 2);
  int i, export_entries_count = *(int*)((char*)module + sizeof(int) * 2 + sizeof(struct list_head) + sizeof(void*) + sizeof(int) * 2 + sizeof(void*));
  static const size_t export_entry_size = sizeof(void*) * 2 + sizeof(int) * 2;

  for(i = 0; i < export_entries_count; i++) {
    void* entry = (char*)export_entries + export_entry_size * i;

    JSAtom* export_name = (JSAtom*)(char*)entry + sizeof(void*) * 2 + sizeof(int) * 2;

    if(*export_name == name)
      return entry;
  }

  return 0;
}

JSValue
js_module_import_meta(JSContext* ctx, const char* name) {
  JSModuleDef* m;
  JSValue ret = JS_UNDEFINED;

  if((m = js_module_loader(ctx, name, 0))) {
    ret = JS_GetImportMeta(ctx, m);
  }
  return ret;
}

extern JSModuleDef* js_module_loader(JSContext* ctx, const char* module_name, void* opaque);

int64_t
js_arraybuffer_length(JSContext* ctx, JSValueConst buffer) {
  size_t len;

  if(JS_GetArrayBuffer(ctx, &len, buffer))
    return len;

  return -1;
}

BOOL
js_atom_is_string(JSContext* ctx, JSAtom atom) {
  JSValue value;
  BOOL ret;
  value = JS_AtomToValue(ctx, atom);
  ret = JS_IsString(value);
  JS_FreeValue(ctx, value);
  return ret;
}

BOOL
js_is_generator(JSContext* ctx, JSValueConst value) {
  const char* str;
  BOOL ret = FALSE;

  if((str = JS_ToCString(ctx, value))) {
    const char* s = str;

    if(!strncmp(s, "async ", 6))
      s += 6;

    if(!strncmp(s, "function", 8)) {
      s += 8;

      while(*s == ' ') ++s;

      if(*s == '*')
        ret = TRUE;
    }

    JS_FreeCString(ctx, str);
  }
  return ret;
}

BOOL
js_is_async(JSContext* ctx, JSValueConst value) {
  const char* str;
  BOOL ret = FALSE;
  if((str = JS_ToCString(ctx, value))) {
    const char* s = str;

    if(!strncmp(s, "async ", 6))
      ret = TRUE;

    else if(!strncmp(s, "[object Async", 13))
      ret = TRUE;

    JS_FreeCString(ctx, str);
  }
  return ret;
}

char*
wsi_peer(struct lws* wsi) {
  char buf[1024];

  lws_get_peer_simple(wsi, buf, sizeof(buf) - 1);

  return strdup(buf);
}

char*
wsi_host(struct lws* wsi) {
  return wsi_token(wsi, lws_wsi_is_h2(wsi) ? WSI_TOKEN_HTTP_COLON_AUTHORITY : WSI_TOKEN_HOST);
}
int
wsi_copy_fragment(struct lws* wsi, enum lws_token_indexes token, int fragment, DynBuf* db) {
  int ret = 0, len;
  // dbuf_init2(&dbuf, 0, 0);

  len = lws_hdr_fragment_length(wsi, token, fragment);

  dbuf_realloc(db, (len > 0 ? len : 1023) + 1);

  if((ret = lws_hdr_copy_fragment(wsi, (void*)db->buf, db->size, token, fragment)) < 0)
    return ret;

  return len;
}
const char*
wsi_vhost_name(struct lws* wsi) {
  struct lws_vhost* vhost;

  if((vhost = lws_get_vhost(wsi)))
    return lws_get_vhost_name(vhost);

  return 0;
}

enum lws_token_indexes
wsi_uri_token(struct lws* wsi) {

  size_t i;

  for(i = 0; i < countof(wsi_uri_tokens); i++)
    if(wsi_token_exists(wsi, wsi_uri_tokens[i]))
      return wsi_uri_tokens[i];

  return -1;
}

bool
opaque_valid(struct wsi_opaque_user_data* opaque) {
  struct list_head* el;

  if(opaque_list.next == 0 && opaque_list.prev == 0)
    init_list_head(&opaque_list);

  list_for_each(el, &opaque_list) if(opaque == list_entry(el, struct wsi_opaque_user_data, link)) return true;

  return false;
}

char*
request_dump(Request const* req, JSContext* ctx) {
  static char buf[2048];
  request_format(req, buf, sizeof(buf), ctx);
  return buf;
}

Request*
request_fromobj(JSValueConst options, JSContext* ctx) {
  Request* req;
  JSValue value;
  const char *url, *path, *method;

  if(!(req = request_alloc(ctx)))
    return req;

  value = JS_GetPropertyStr(ctx, options, "url");
  url = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, options, "path");
  path = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  JS_GetPropertyStr(ctx, options, "method");
  method = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  JS_GetPropertyStr(ctx, options, "headers");

  JS_FreeValue(ctx, value);

  request_init(req, [object Object] url_create(url, ctx), method_number(method));

  JS_FreeCString(ctx, url);
  JS_FreeCString(ctx, path);
  JS_FreeCString(ctx, method);

  return req;
}

Request*
request_fromurl(const char* uri, JSContext* ctx) {
  HTTPMethod method = METHOD_GET;
  struct url url = url_create(uri, ctx);

  return request_new(url, method, ctx);
}

void
request_zero(Request* req) {
  memset(req, 0, sizeof(Request));
  req->headers = BUFFER_0();
  req->body = 0;
}

void
request_free(Request* req, JSContext* ctx) {
  if(--req->ref_count == 0) {
    request_clear(req, ctx);
    js_free(ctx, req);
  }
}

char*
response_dump(const struct http_response* resp) {
  static char buf[1024];
  response_format(resp, buf, sizeof(buf));
  return buf;
}

void
response_zero(struct http_response* resp) {
  memset(resp, 0, sizeof(Response));
  resp->body = BUFFER_0();
}
ssize_t
response_write(struct http_response* resp, const void* x, size_t n, JSContext* ctx) {
  assert(resp->generator);
  return generator_write(resp->generator, x, n, JS_UNDEFINED);
}

struct http_response*
response_redirect(struct http_response* resp, const char* location, JSContext* ctx) {

  resp->status = 302;
  // url_parse(&resp->url, location, ctx);
  headers_set(ctx, &resp->headers, "Location", location);
  return resp;
}

struct ringbuffer*
ringbuffer_new2(size_t element_len, size_t count, JSContext* ctx) {
  struct ringbuffer* rb;

  if((rb = ringbuffer_new(ctx)))
    ringbuffer_init2(rb, element_len, count);

  return rb;
}

struct context*
session_context(struct session_data* sess) {
  struct lws* wsi;

  if((wsi = session_wsi(sess)))
    return wsi_context(wsi);

  return 0;
}

struct http_request*
session_request(struct session_data* sess) {
  if(JS_IsObject(sess->req_obj))
    return minnet_request_data(sess->req_obj);
  return 0;
}

int
url_connect(struct url* url, struct lws_context* context, struct lws** p_wsi) {
  struct lws_client_connect_info i;

  url_info(url, &i);

  i.context = context;
  i.pwsi = p_wsi;

  return !lws_client_connect_via_info(&i);
}

char*
url_location(const struct url url, JSContext* ctx) {
  const char* query;
  if((query = url_query(url)))
    return js_strndup(ctx, url.path, query - url.path);
  return js_strdup(ctx, url.path);
}

void
url_dump(const char* n, struct url const* url) {
  fprintf(stderr, "%s{ protocol = %s, host = %s, port = %u, path = %s }\n", n, url->protocol, url->host, url->port, url->path);
  fflush(stderr);
}

void
js_value_dump(JSContext* ctx, const char* n, JSValueConst const* v) {
  const char* str = JS_ToCString(ctx, *v);
  lwsl_user("%s = '%s'\n", n, str);
  JS_FreeCString(ctx, str);
}

int
socket_geterror(int fd) {
  int e;
  socklen_t sl = sizeof(e);

  if(!getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &sl)) {
    setsockopt(fd, SOL_SOCKET, SO_ERROR, &e, sl);
    return e;
  }

  return -1;
}

char*
socket_address(int fd, int (*fn)(int, struct sockaddr*, socklen_t*)) {
  const char* s = 0;
  union {
    struct sockaddr a;
    struct sockaddr_in ai;
    struct sockaddr_in6 ai6;
  } sa;
  socklen_t sl = sizeof(s);
  uint16_t port = 0;
  static char addr[1024];

  if(fn(fd, &sa.a, &sl) != -1) {
    size_t i;
    s = inet_ntop(sa.ai.sin_family, sa.ai.sin_family == AF_INET ? (void*)&sa.ai.sin_addr : (void*)&sa.ai6.sin6_addr, addr, sizeof(addr));
    i = strlen(s);

    switch(sa.ai.sin_family) {
      case AF_INET: port = ntohs(sa.ai.sin_port); break;
      case AF_INET6: port = ntohs(sa.ai6.sin6_port); break;
    }
    snprintf(&addr[i], sizeof(addr) - i, ":%u", port);
  }

  return (char*)s;
}

int
ws_writable(struct socket* ws, BOOL binary, JSContext* ctx) {
  struct wsi_opaque_user_data* opaque;
  int ret = 0;

  if((opaque = lws_get_opaque_user_data(ws->lwsi))) {
    struct session_data* session = opaque->sess;

    ret = session_writable(session, binary, ctx);
  }
  return ret;
}

typedef struct {
  JSContext* ctx;
  struct socket* ws;
} WSWantWrite;

JSValue
ws_want_write(struct socket* ws, JSContext* ctx) {
  WSWantWrite* h;

  if(!(h = js_mallocz(ctx, sizeof(WSWantWrite))))
    return JS_ThrowOutOfMemory(ctx);

  *h = (WSWantWrite){ctx, ws_dup(ws)};

  return js_function_cclosure(ctx, want_write, 0, 0, h, ws_want_write_free);
}
void
form_parser_zero(struct form_parser* fp) {
  fp->ws = 0;
  fp->spa = 0;
  fp->lwsac_head = 0;
  memset(&fp->spa_create_info, 0, sizeof(struct lws_spa_create_info));
}

JSValue
vector2array(JSContext* ctx, int argc, JSValueConst argv[]) {
  int i;
  JSValue ret = JS_NewArray(ctx);
  for(i = 0; i < argc; i++) JS_SetPropertyUint32(ctx, ret, i, argv[i]);
  return ret;
}

JSValue
js_function_bind_v(JSContext* ctx, JSValueConst func, ...) {
  va_list args;
  DynBuf b;
  JSValueConst arg;
  dbuf_init2(&b, ctx, (DynBufReallocFunc*)js_realloc);
  va_start(args, func);
  while((arg = va_arg(args, JSValueConst))) { dbuf_put(&b, &arg, sizeof(JSValueConst)); }
  va_end(args);
  return js_function_bind(ctx, func, b.size / sizeof(JSValueConst), (JSValueConst*)b.buf);
}

JSBuffer
js_buffer_data(JSContext* ctx, const void* data, size_t size) {
  ByteBlock block = {(uint8_t*)data, (uint8_t*)data + size};

  return js_buffer_fromblock(ctx, &block);
}

void
js_buffer_to3(JSBuffer buf, const char** pstr, void** pptr, unsigned* plen) {
  if(!JS_IsString(buf.value)) {
    size_t len = 0;
    js_buffer_to(buf, pptr, &len);
    if(plen)
      *plen = len;
  } else
    *pstr = (const char*)buf.data;
}

BOOL
js_buffer_valid(const JSBuffer* in) {
  return !JS_IsException(in->value);
}

JSBuffer
js_buffer_clone(const JSBuffer* in, JSContext* ctx) {
  JSBuffer buf = js_input_buffer(ctx, in->value);
  buf.pos = in->pos;
  buf.range = in->range;
  return buf;
}

void
js_buffer_dump(const JSBuffer* in, DynBuf* db) {
  dbuf_printf(db, "(JSBuffer){ .data = %p, .size = %zu, .free = %p }", in->data, in->size, in->free);
}

BOOL
js_is_iterable(JSContext* ctx, JSValueConst obj) {
  JSAtom atom;
  BOOL ret = FALSE;
  atom = js_symbol_static_atom(ctx, "iterator");
  if(JS_HasProperty(ctx, obj, atom))
    ret = TRUE;

  JS_FreeAtom(ctx, atom);
  if(!ret) {
    atom = js_symbol_static_atom(ctx, "asyncIterator");
    if(JS_HasProperty(ctx, obj, atom))
      ret = TRUE;

    JS_FreeAtom(ctx, atom);
  }
  return ret;
}

int64_t
js_get_propertystr_int64(JSContext* ctx, JSValueConst obj, const char* str) {
  int64_t ret = 0;
  JSValue value;
  value = JS_GetPropertyStr(ctx, obj, str);
  JS_ToInt64(ctx, &ret, value);
  JS_FreeValue(ctx, value);
  return ret;
}

JSModuleDef*
js_module_at(JSContext* ctx, int i) {
  struct list_head *el = 0, *list = js_module_list(ctx);

  list_for_each(list, el) {
    JSModuleDef* module = (void*)((char*)el - sizeof(JSAtom) * 2);

    if(i-- == 0)
      return module;
  }
  return 0;
}

JSValue
minnet_request_from(JSContext* ctx, int argc, JSValueConst argv[]) {
  MinnetRequest* req;

  [object Object] req = request_from(argc, argv, ctx);

  return minnet_request_wrap(ctx, req);
}

JSValue
minnet_request_new(JSContext* ctx, MinnetURL url, enum http_method method) {
  MinnetRequest* req;

  if(!(req = request_new(url, method, ctx)))
    return JS_ThrowOutOfMemory(ctx);

  return minnet_request_wrap(ctx, req);
}

JSValue
minnet_generator_reader(JSContext* ctx, MinnetGenerator* gen) {
  JSValue ret = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, ret, "read", js_function_cclosure(ctx, minnet_generator_next, 0, 0, generator_dup(gen), (void*)&generator_free));
  JS_SetPropertyFunctionList(ctx, ret, minnet_generator_funcs, countof(minnet_generator_funcs));

  return ret;
}

JSValue
minnet_generator_iterator(JSContext* ctx, MinnetGenerator* gen) {
  JSValue ret = JS_NewObject(ctx);

  ++gen->ref_count;

  JS_SetPropertyStr(ctx, ret, "next", js_function_cclosure(ctx, minnet_generator_next, 0, 0, gen_p, (void*)&generator_free));

  return ret;
}

MinnetClient*
client_find(struct lws* wsi) {
  struct list_head* el;

  list_for_each_prev(el, &minnet_clients) {
    MinnetClient* client = list_entry(el, MinnetClient, link);

    if(client->wsi == wsi)
      return client;
  }
  return 0;
}
JSValue
minnet_form_parser_new(JSContext* ctx, MinnetWebsocket* ws, int nparams, const char* const* param_names, size_t chunk_size) {
  MinnetFormParser* fp;

  if(!(fp = form_parser_new(ctx, ws, nparams, param_names, chunk_size)))
    return JS_ThrowOutOfMemory(ctx);

  return minnet_form_parser_wrap(ctx, fp);
}

JSValue
minnet_ringbuffer_wrap(JSContext* ctx, struct ringbuffer* rb) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_ringbuffer_proto, minnet_ringbuffer_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, rb);

  ++rb->ref_count;

  return ret;
}

static JSValue
tail_consume(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  JSValue ret = JS_UNDEFINED;
  struct ringbuffer_tail* tail = opaque;
  struct lws_ring* r = tail->rb->ring;
  uint32_t wanted;
  JSBuffer buf;

  int index = js_buffer_fromargs(ctx, argc, argv, &buf);

  js_input_args(ctx, argc, argv);

  if(buf.size) {
    wanted = buf.size / ringbuffer_element_len(tail->rb);
  } else {
    if(index == argc || JS_ToUint32(ctx, &wanted, argv[index]))
      return JS_ThrowRangeError(ctx, "need buffer or element count");
  }

  ret = JS_NewUint32(ctx, lws_ring_consume(r, tail->ptr, buf.data, wanted));

  js_buffer_free(&buf, ctx);

  return ret;
}

static JSValue
tail_bind(JSContext* ctx, JSValue func, JSValueConst this_val, JSValueConst arg) {
  JSValue ret = js_function_bind_this_1(ctx, func, this_val, arg);
  JS_FreeValue(ctx, func);
  return ret;
}

static void
client_resolved_free(void* ptr) {
  HTTPAsyncResolveClosure* closure = ptr;
  if(--closure->ref_count == 0) {
    response_free(closure->resp, closure->ctx);
    js_free(closure->ctx, ptr);
  }
}

static JSValue
client_resolved(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  const char* val = JS_ToCString(ctx, argv[0]);

  // printf("value=%s\n", val);
  LOG(__func__, "value=%s", val);
  JS_FreeCString(ctx, val);

  return JS_UNDEFINED;
}

static JSValue
client_promise(JSContext* ctx, struct session_data* session, MinnetResponse* resp, struct lws* wsi, JSValueConst value) {
  HTTPAsyncResolveClosure* p;
  JSValue ret = JS_UNDEFINED;

  if((p = js_malloc(ctx, sizeof(HTTPAsyncResolveClosure)))) {
    *p = (HTTPAsyncResolveClosure){1, ctx, session, response_dup(resp), wsi};
    JSValue fn = js_function_cclosure(ctx, client_resolved, 1, 0, p, client_resolved_free);
    JSValue tmp = js_promise_then(ctx, value, fn);
    JS_FreeValue(ctx, fn);
    ret = tmp;
  } else {
    ret = JS_ThrowOutOfMemory(ctx);
  }
  return ret;
}

char*
buffer_escaped(ByteBuffer const* buf) {
  char* ptr;
  ByteBuffer out;
  size_t size = buffer_REMAIN(buf) * 4;

  size = (size + 8) & (~7);

  if(!(ptr = malloc(size)))
    return 0;

  out = BUFFER_N(ptr, size - 1);

  ptr[buffer_escape(&out, buf->read, buffer_REMAIN(buf))] = '\0';

  return ptr;
}

int
buffer_fromvalue(ByteBuffer* buf, JSValueConst value, JSContext* ctx) {
  int ret = -1;
  JSBuffer input = js_input_chars(ctx, value);

  if(input.data == 0 || input.size == 0) {
    ret = 0;
  } else if(buffer_append(buf, input.data, input.size) == input.size) {
    ret = 1;
  }

  js_buffer_free(&input, ctx);
  return ret;
}

JSValue
buffer_tostring(ByteBuffer const* buf, JSContext* ctx) {
  return JS_NewStringLen(ctx, (const char*)buf->start, buffer_HEAD(buf));
}

char*
headers_gettoken(JSContext* ctx, struct lws* wsi, enum lws_token_indexes tok) {
  int len;

  if((len = lws_hdr_total_length(wsi, tok)) > 0) {
    char* hdr;

    if((hdr = js_malloc(ctx, len + 1))) {
      lws_hdr_copy(wsi, hdr, len + 1, tok);
      hdr[len] = '\0';
      return hdr;
    }
  }
  return 0;
}

struct list_head*
js_module_list(JSContext* ctx) {
  void* tmp_opaque;
  ptrdiff_t needle;
  void** ptr;
  tmp_opaque = JS_GetContextOpaque(ctx);
  memset(&needle, 0xa5, sizeof(needle));
  JS_SetContextOpaque(ctx, (void*)needle);

  ptr = memmem(ctx, 1024, &needle, sizeof(needle));

  JS_SetContextOpaque(ctx, tmp_opaque);

  return ((struct list_head*)(ptr - 2)) - 1;
}

void
opaque_clear(struct wsi_opaque_user_data* opaque, JSContext* ctx) {
  opaque_clear_rt(opaque, JS_GetRuntime(ctx));
}
size_t
buffer_escape(ByteBuffer* buf, const void* x, size_t len) {
  const uint8_t *ptr, *end;

  size_t prev = buffer_REMAIN(buf);

  for(ptr = x, end = (const uint8_t*)x + len; ptr < end; ptr++) {
    char c = *ptr;

    if(buffer_AVAIL(buf) < 4)
      break;

    switch(c) {
      case '\n':
        buffer_putchar(buf, '\\');
        buffer_putchar(buf, 'n');
        break;
      case '\r':
        buffer_putchar(buf, '\\');
        buffer_putchar(buf, 'r');
        break;
      case '\t':
        buffer_putchar(buf, '\\');
        buffer_putchar(buf, 't');
        break;
      case '\v':
        buffer_putchar(buf, '\\');
        buffer_putchar(buf, 'v');
        break;
      case '\b':
        buffer_putchar(buf, '\\');
        buffer_putchar(buf, 'b');
        break;
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 12:
      case 14:
      case 15:
      case 16:
      case 17:
      case 18:
      case 19:
      case 20:
      case 21:
      case 22:
      case 23:
      case 24:
      case 25:
      case 26:
      case 27:
      case 28:
      case 29:
      case 30:
      case 31: buffer_printf(buf, "\\x%02", c); break;
      default: buffer_putchar(buf, c); break;
    }
  }
  return buffer_REMAIN(buf) - prev;
}

struct form_parser*
form_parser_dup(struct form_parser* fp) {
  ++fp->ref_count;
  return fp;
}

Deferred*
deferred_dupjs(JSValueConst value, JSContext* ctx) {
  JSValue v = JS_DupValue(ctx, value);
  return deferred_newjs(v, ctx);
}

JSValue
deferred_tojs(Deferred* def, JSContext* ctx) {
  deferred_dup(def);

  return js_function_cclosure(ctx, deferred_js_call, 0, 0, def, (void (*)(ptr_t))deferred_free);
}

static JSValue
deferred_js_call(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, ptr_t ptr) {
  Deferred* def = ptr;

  return deferred_call(def).js;
}

BOOL
buffer_putchar(ByteBuffer* buf, char c) {
  if(buf->write + 1 <= buf->end) {
    *buf->write = (uint8_t)c;
    buf->write++;
    return TRUE;
  }
  return FALSE;
}

void
ringbuffer_init2(struct ringbuffer* rb, size_t element_len, size_t count) {
  const char* type = "application/binary";
  ringbuffer_init(rb, element_len, count, type, strlen(type));
}
const void*
ringbuffer_next(struct ringbuffer* rb) {
  assert(rb->ring);
  return lws_ring_get_element(rb->ring, 0);
}

void
response_free(struct http_response* resp, JSContext* ctx) {
  if(--resp->ref_count == 0) {
    response_clear(resp, ctx);
    js_free(ctx, resp);
  }
}

void
response_clear(struct http_response* resp, JSContext* ctx) {
  url_free(&resp->url, ctx);
  if(resp->type) {
    js_free(ctx, (void*)resp->type);
    resp->type = 0;
  }

  buffer_free(&resp->headers);
  generator_destroy(&resp->generator);
}

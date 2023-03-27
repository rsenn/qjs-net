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

ssize_t
headers_unsetb(ByteBuffer* buffer, const char* name, size_t namelen) {
  ssize_t pos;

  if((pos = headers_findb(buffer, name, namelen)) >= 0) {
    uint8_t* ptr = buffer->start + pos;
    size_t len = byte_chrs(ptr, buffer->write - ptr, "\r\n", 2);

    while(isspace(buffer->start[len]) && buffer->start + len < buffer->write) ++len;

    memcpy(ptr, ptr + len, buffer->write - (buffer->start + len));
    buffer->write -= len;

    if(buffer->write < buffer->end)
      memset(buffer->write, 0, buffer->end - buffer->write);
  }
  return pos;
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
#define _GNU_SOURCE
#include "minnet-server.h"
#include "minnet-client.h"
#include "minnet-request.h"
#include "minnet-response.h"
#include "minnet-websocket.h"
#include "minnet-ringbuffer.h"
#include "minnet-generator.h"
#include "minnet-form-parser.h"
#include "minnet-hash.h"
#include "jsutils.h"
#include "minnet-buffer.h"
#include <libwebsockets.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/time.h>
#include <arpa/inet.h>

/*#ifdef _WIN32
#include "poll.h"
#endif*/

#ifndef POLLIN
#define POLLIN 1
#endif
#ifndef POLLOUT
#define POLLOUT 4
#endif
#ifndef POLLERR
#define POLLERR 8
#endif
#ifndef POLLHUP
#define POLLHUP 16
#endif
/*
#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif
*/
#define PIO (POLLIN | POLLOUT | POLLERR)

struct handler_closure {
  JSContext* ctx;
  struct lws* lwsi;
  struct wsi_opaque_user_data* opaque;
};

JSValue minnet_fetch(JSContext*, JSValueConst, int, JSValueConst*);

// THREAD_LOCAL struct lws_context* minnet_lws_context = 0;

static THREAD_LOCAL JSValue minnet_log_cb, minnet_log_this;
THREAD_LOCAL int32_t minnet_log_level = 0;
THREAD_LOCAL JSContext* minnet_log_ctx = 0;
THREAD_LOCAL struct list_head minnet_sockets = {0, 0};
static THREAD_LOCAL uint32_t session_serial = 0;
// THREAD_LOCAL BOOL minnet_exception = FALSE;

static size_t
skip_brackets(const char* line, size_t len) {
  size_t n = 0;
  if(len > 0 && line[0] == '[') {
    if((n = byte_chr(line, len, ']')) < len)
      n++;
    while(n < len && isspace(line[n])) n++;
    if(n + 1 < len && line[n + 1] == ':')
      n += 2;
    while(n < len && (isspace(line[n]) || line[n] == '-')) n++;
  }

  return n;
}

static size_t
skip_directory(const char* line, size_t len) {
  if(line[0] == '/') {
    size_t colon = byte_chr(line, len, ':');
    size_t slash = byte_rchr(line, colon, '/');

    if(slash < colon)
      return slash + 1;
  }

  return 0;
}

static size_t
strip_trailing_newline(const char* line, size_t* len_p) {
  size_t len = *len_p;
  while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
  return *len_p = len;
}

void
minnet_log_callback(int level, const char* line) {
  if(minnet_log_ctx) {
    size_t n = 0, len = strlen(line);

    if(JS_IsFunction(minnet_log_ctx, minnet_log_cb)) {

      n = skip_brackets(line, len);
      line += n;
      len -= n;
      n = skip_directory(line, len);
      line += n;
      len -= n;

      strip_trailing_newline(line, &len);

      JSValueConst argv[2] = {
          JS_NewInt32(minnet_log_ctx, level),
          JS_NewStringLen(minnet_log_ctx, line, len),
      };
      JSValue ret = JS_Call(minnet_log_ctx, minnet_log_cb, minnet_log_this, 2, argv);

      if(JS_IsException(ret)) {
        JSValue exception = JS_GetException(minnet_log_ctx);
        JS_FreeValue(minnet_log_ctx, exception);
      }

      JS_FreeValue(minnet_log_ctx, argv[0]);
      JS_FreeValue(minnet_log_ctx, argv[1]);
      JS_FreeValue(minnet_log_ctx, ret);
    } else {
      js_console_log(minnet_log_ctx, &minnet_log_this, &minnet_log_cb);
    }
  }
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

void
session_zero(MinnetSession* session) {
  memset(session, 0, sizeof(MinnetSession));
  session->serial = -1;
  session->ws_obj = JS_NULL;
  session->req_obj = JS_NULL;
  session->resp_obj = JS_NULL;
  session->generator = JS_NULL;
  session->next = JS_NULL;

  session->serial = ++session_serial;

  // list_add(&session->link, &minnet_sessions);

  // printf("%s #%i %p\n", __func__, session->serial, session);
}

void
session_clear(MinnetSession* session, JSContext* ctx) {
  // list_del(&session->link);

  JS_FreeValue(ctx, session->ws_obj);
  JS_FreeValue(ctx, session->req_obj);
  JS_FreeValue(ctx, session->resp_obj);
  JS_FreeValue(ctx, session->generator);
  JS_FreeValue(ctx, session->next);

  buffer_free(&session->send_buf, JS_GetRuntime(ctx));

  // printf("%s #%i %p\n", __func__, session->serial, session);
}

struct http_response*
session_response(MinnetSession* session, MinnetCallback* cb) {
  MinnetResponse* resp = minnet_response_data2(cb->ctx, session->resp_obj);

  if(cb && cb->ctx) {
    JSValue ret = minnet_emit_this(cb, session->ws_obj, 2, session->args);
    lwsl_user("session_response ret=%s", JS_ToCString(cb->ctx, ret));
    if(JS_IsObject(ret) && minnet_response_data2(cb->ctx, ret)) {
      JS_FreeValue(cb->ctx, session->args[1]);
      session->args[1] = ret;
      resp = minnet_response_data2(cb->ctx, ret);
    } else {
      JS_FreeValue(cb->ctx, ret);
    }
  }
  lwsl_user("session_response %s", response_dump(resp));

  return resp;
}

JSValue
session_object(struct wsi_opaque_user_data* opaque, JSContext* ctx) {
  JSValue ret;
  ret = JS_NewArray(ctx);

  JS_SetPropertyUint32(ctx, ret, 0, opaque->serial ? JS_NewInt32(ctx, opaque->serial) : JS_NULL);

  if(opaque->sess) {
    JS_SetPropertyUint32(ctx, ret, 1, JS_DupValue(ctx, opaque->sess->ws_obj));
    JS_SetPropertyUint32(ctx, ret, 2, JS_DupValue(ctx, opaque->sess->req_obj));
    JS_SetPropertyUint32(ctx, ret, 3, JS_DupValue(ctx, opaque->sess->resp_obj));
  }
  return ret;
}

BOOL
context_exception(MinnetContext* context, JSValue retval) {
  if(JS_IsException(retval)) {
    context->exception = TRUE;
    JSValue exception = JS_GetException(context->js);
    JSValue stack = JS_GetPropertyStr(context->js, exception, "stack");
    const char* err = JS_ToCString(context->js, exception);
    const char* stk = JS_ToCString(context->js, stack);
    // printf("Got exception: %s\n%s\n", err, stk);
    JS_FreeCString(context->js, err);
    JS_FreeCString(context->js, stk);
    JS_FreeValue(context->js, stack);
    JS_Throw(context->js, exception);
  }

  return context->exception;
}

void
context_clear(MinnetContext* context) {
  JSContext* ctx = context->js;

  lws_set_log_level(0, 0);

  lws_context_destroy(context->lws);
  lws_set_log_level(((unsigned)minnet_log_level & ((1u << LLL_COUNT) - 1)), minnet_log_callback);

  JS_FreeValue(ctx, context->crt);
  JS_FreeValue(ctx, context->key);
  JS_FreeValue(ctx, context->ca);

  JS_FreeValue(ctx, context->error);
}

MinnetClosure*
closure_new(JSContext* ctx) {
  MinnetClosure* closure;

  if((closure = js_mallocz(ctx, sizeof(MinnetClosure))))
    closure->ref_count = 1;

  return closure;
}

MinnetClosure*
closure_dup(MinnetClosure* c) {
  ++c->ref_count;
  return c;
}

void
closure_free(void* ptr) {
  MinnetClosure* closure = ptr;

  if(--closure->ref_count == 0) {
    if(closure->server) {
      JSContext* ctx = closure->context->js;
      // printf("%s server=%p\n", __func__, closure->server);
      if(closure->free_func)
        closure->free_func(closure->context);

      js_free(ctx, closure);
    }
  }
}

int
minnet_lws_unhandled(const char* handler, int reason) {
  lwsl_warn("Unhandled \x1b[1;31m%s\x1b[0m event: %i %s\n", handler, reason, lws_callback_name(reason));
  assert(0);
  return -1;
}

/*static JSValue
get_log(JSContext* ctx, JSValueConst this_val) {
  return JS_DupValue(ctx, minnet_log_cb);
}*/

static JSValue
set_log(JSContext* ctx, JSValueConst this_val, JSValueConst value, JSValueConst thisObj) {
  JSValue ret = JS_VALUE_GET_TAG(minnet_log_cb) == 0 ? JS_UNDEFINED : minnet_log_cb;

  minnet_log_ctx = ctx;
  minnet_log_cb = JS_DupValue(ctx, value);

  if(!JS_IsUndefined(minnet_log_this) && !JS_IsNull(minnet_log_this) && JS_VALUE_GET_TAG(minnet_log_this) != 0)
    JS_FreeValue(ctx, minnet_log_this);

  minnet_log_this = JS_DupValue(ctx, thisObj);

  return ret;
}

static JSValue
minnet_set_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  if(argc >= 1 && JS_IsNumber(argv[0])) {
    JS_ToInt32(ctx, &minnet_log_level, argv[0]);
    argc--;
    argv++;
  }

  ret = set_log(ctx, this_val, argv[0], argc > 1 ? argv[1] : JS_NULL);
  lws_set_log_level(((unsigned)minnet_log_level & ((1u << LLL_COUNT) - 1)), minnet_log_callback);
  return ret;
}

JSValue
headers_object(JSContext* ctx, const void* start, const void* e) {
  JSValue ret = JS_NewObject(ctx);
  size_t len, namelen, n;
  const uint8_t *x, *end;

  for(x = start, end = e; x < end; x += len + 1) {
    len = byte_chrs(x, end - x, "\r\n", 2);
    if(len > (n = byte_chr(x, len, ':'))) {
      const char* prop = (namelen = n) ? js_strndup(ctx, (const char*)x, namelen) : 0;
      if(x[n] == ':')
        n++;
      if(isspace(x[n]))
        n++;
      if(prop) {
        JS_DefinePropertyValueStr(ctx, ret, prop, JS_NewStringLen(ctx, (const char*)&x[n], len - n), JS_PROP_ENUMERABLE);
        js_free(ctx, (void*)prop);
      }
    }
  }
  return ret;
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

int
headers_addobj(MinnetBuffer* buffer, struct lws* wsi, JSValueConst obj, JSContext* ctx) {
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

size_t
headers_write(uint8_t** in, uint8_t* end, MinnetBuffer* buffer, struct lws* wsi) {
  uint8_t *r = buffer->read, *w = buffer->write, *next, *start, *ptr;

  start = ptr = *in;

  while(r < w) {
    size_t l = byte_chr(r, w - r, '\n');
    size_t n, m = byte_chr(r, l, ':');
    uint8_t* name = r;

    next = r + l + 1;

    n = m;
    ++m;

    while(r[m] && r[m] == ' ') ++m;

    r += m;
    l -= m;

    // if(r + l < w)
    while(l > 0 && (r[l - 1] == '\n' || r[l - 1] == '\r')) --l;
    uint8_t tmp = name[n + 1];
    name[n + 1] = '\0';

    /*int ret = */ lws_add_http_header_by_name(wsi, name, r, l, &ptr, end);
    name[n + 1] = tmp;

#ifdef DEBUG_OUTPUT
    printf("name=%.*s value=%.*s lws_add_http_header_by_name() = %d\n", (int)n, name, (int)l, r, ret);
#endif
    r = next;
  }

  *in = ptr;
  return ptr - start;
}

int
headers_fromobj(MinnetBuffer* buffer, JSValueConst obj, JSContext* ctx) {
  JSPropertyEnum* tab;
  uint32_t tab_len, i;

  if(JS_GetOwnPropertyNames(ctx, &tab, &tab_len, obj, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK))
    return 0;

  for(i = 0; i < tab_len; i++) {
    JSValue jsval = JS_GetProperty(ctx, obj, tab[i].atom);
    size_t value_len, prop_len;
    const char *value, *prop;

    value = JS_ToCStringLen(ctx, &value_len, jsval);
    JS_FreeValue(ctx, jsval);

    prop = JS_AtomToCString(ctx, tab[i].atom);
    prop_len = strlen(prop);

    buffer_grow(buffer, prop_len + 2 + value_len + 2, ctx);

    buffer_write(buffer, prop, prop_len);
    buffer_write(buffer, ": ", 2);
    buffer_write(buffer, value, value_len);
    buffer_write(buffer, "\r\n", 2);

    JS_FreeCString(ctx, prop);
    JS_FreeCString(ctx, value);
  }

  js_free(ctx, tab);
  return i;
}

ssize_t
headers_set(JSContext* ctx, MinnetBuffer* buffer, const char* name, const char* value) {
  size_t namelen = strlen(name), valuelen = strlen(value);
  size_t len = namelen + 2 + valuelen + 2;

  buffer_grow(buffer, len, ctx);
  buffer_write(buffer, name, namelen);
  buffer_write(buffer, ": ", 2);
  buffer_write(buffer, value, valuelen);
  buffer_write(buffer, "\r\n", 2);

  return len;
}

ssize_t
headers_findb(MinnetBuffer* buffer, const char* name, size_t namelen) {
  uint8_t* ptr;
  ssize_t ret = 0;

  for(ptr = buffer->start; ptr < buffer->write;) {
    size_t len = byte_chrs(ptr, buffer->write - ptr, "\r\n", 2);

    printf("%s %.*s\n", __func__, (int)len, (char*)ptr);

    if(!strncasecmp((const char*)ptr, name, namelen) && ptr[namelen] == ':')
      return ret;
    while(isspace(ptr[len]) && ptr + len < buffer->write) ++len;
    ptr += len;
    ++ret;
  }

  return -1;
}

char*
headers_at(MinnetBuffer* buffer, size_t* lenptr, size_t index) {
  uint8_t* ptr;
  size_t i = 0;
  for(ptr = buffer->start; ptr < buffer->write;) {
    size_t len = byte_chrs(ptr, buffer->write - ptr, "\r\n", 2);
    if(i == index) {
      if(lenptr)
        *lenptr = len;
      return (char*)ptr;
    }
    while(isspace(ptr[len]) && ptr + len < buffer->write) ++len;
    ptr += len;
    ++i;
  }
  return 0;
}

char*
headers_get(MinnetBuffer* buffer, size_t* lenptr, const char* name) {
  ssize_t index;

  if((index = headers_find(buffer, name)) != -1) {
    size_t l, n;
    char* ret = headers_at(buffer, &l, index);
    n = scan_nonwhitenskip(ret, l);
    ret += n;
    l -= n;
    n = scan_whitenskip(ret, l);
    ret += n;
    l -= n;
    if(lenptr)
      *lenptr = l;
    return ret;
  }

  return 0;
}

ssize_t
headers_copy(MinnetBuffer* buffer, char* dest, size_t sz, const char* name) {
  char* hdr;
  size_t len;

  if((hdr = headers_get(buffer, &len, name))) {
    len = MIN(len, sz);

    strncpy(dest, hdr, len);
    return len;
  }

  return -1;
}

ssize_t
headers_find(MinnetBuffer* buffer, const char* name) {
  return headers_findb(buffer, name, strlen(name));
}

ssize_t
headers_unsetb(MinnetBuffer* buffer, const char* name, size_t namelen) {
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

ssize_t
headers_unset(MinnetBuffer* buffer, const char* name) {
  return headers_unsetb(buffer, name, strlen(name));
}

int
headers_tostring(JSContext* ctx, MinnetBuffer* headers, struct lws* wsi) {
  int tok, len, count = 0;

  if(!headers->start)
    buffer_alloc(headers, 1024, ctx);

  for(tok = WSI_TOKEN_HOST; tok < WSI_TOKEN_COUNT; tok++) {
    if(tok == WSI_TOKEN_HTTP || tok == WSI_TOKEN_HTTP_URI_ARGS)
      continue;

    if((len = lws_hdr_total_length(wsi, tok)) > 0) {
      char hdr[len + 1];
      const char* name;

      if((name = (const char*)lws_token_to_string(tok))) {
        int namelen = 1 + byte_chr(name + 1, strlen(name + 1), ':');
        lws_hdr_copy(wsi, hdr, len + 1, tok);
        hdr[len] = '\0';

        // printf("headers %i %.*s '%s'\n", tok, namelen, name, hdr);

        if(!headers->alloc)
          buffer_alloc(headers, 1024, ctx);

        while(!buffer_printf(headers, "%.*s: %s\n", namelen, name, hdr)) { buffer_grow(headers, 1024, ctx); }
        ++count;
      }
    }
  }
  return count;
}

int
fd_handler(struct lws* wsi, MinnetCallback* cb, struct lws_pollargs args) {
  JSValue argv[3] = {JS_NewInt32(cb->ctx, args.fd)};

  minnet_handlers(cb->ctx, wsi, args, &argv[1]);
  minnet_emit(cb, 3, argv);

  JS_FreeValue(cb->ctx, argv[0]);
  JS_FreeValue(cb->ctx, argv[1]);
  JS_FreeValue(cb->ctx, argv[2]);
  return 0;
}

int
fd_callback(struct lws* wsi, enum lws_callback_reasons reason, MinnetCallback* cb, struct lws_pollargs* args) {

  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: return 0;

    case LWS_CALLBACK_ADD_POLL_FD: {

      if(cb->ctx) {
        fd_handler(wsi, cb, *args);
      }
      return 0;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {

      if(cb->ctx) {
        fd_handler(wsi, cb, *args);
      }
      return 0;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      if(cb->ctx) {
        if(args->events != args->prev_events) {
          fd_handler(wsi, cb, *args);
        }
      }
      return 0;
    }

    default: {
      return -1;
    }
  }
}

/*static const char*
io_events(int events) {
  switch(events) {
    case POLLOUT | POLLHUP: return "OUT|HUP";
    case POLLIN | POLLOUT | POLLHUP | POLLERR: return "IN|OUT|HUP|ERR";
    case POLLOUT | POLLHUP | POLLERR: return "OUT|HUP|ERR";
    case POLLIN | POLLOUT: return "IN|OUT";
    case POLLIN: return "IN";
    case POLLOUT:
      return "OUT";
  }
  assert(!events);
  return "";
}*/

/*static int
io_parse_events(const char* str) {
  int events = 0;

  if(strstr(str, "IN"))
    events |= POLLIN;
  if(strstr(str, "OUT"))
    events |= POLLOUT;
  if(strstr(str, "HUP"))
    events |= POLLHUP;
  if(strstr(str, "ERR"))
    events |= POLLERR;
  return events;
}*/

static JSValue
minnet_io_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  struct handler_closure* closure = ptr;
  struct pollfd* p;
  int32_t wr;
  JSValue ret = JS_UNDEFINED;

  assert(closure->opaque);
  p = &closure->opaque->poll;

  JS_ToInt32(ctx, &wr, argv[0]);

  p->revents = magic & (wr == WRITE_HANDLER ? POLLOUT : POLLIN);

  if((p->revents & PIO) != magic) {
    if(poll(p, 1, 0) < 0)
      lwsl_err("poll error: %s\n", strerror(errno));
  }

  if(p->revents & PIO) {
    struct lws_pollfd x = {p->fd, magic, p->revents & PIO};

    if(p->revents & (POLLERR | POLLHUP))
      closure->opaque->error = errno;

    // errno = 0;

    ret = JS_NewInt32(ctx, lws_service_fd(lws_get_context(closure->lwsi), &x));
  }

  return ret;
}

static void
free_handler_closure(void* ptr) {
  struct handler_closure* closure = ptr;
  JSContext* ctx = closure->ctx;
  js_free(ctx, closure);
};

static JSValue
make_handler(JSContext* ctx, int fd, int events, struct lws* wsi) {
  struct handler_closure* closure;

  if(!(closure = js_mallocz(ctx, sizeof(struct handler_closure))))
    return JS_ThrowOutOfMemory(ctx);

  *closure = (struct handler_closure){ctx, wsi, lws_opaque(wsi, ctx)};

  closure->opaque->poll = (struct pollfd){fd, events, 0};

  return JS_NewCClosure(ctx, minnet_io_handler, 1, events, closure, free_handler_closure);
}

JSValue
minnet_get_sessions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct list_head* el;
  JSValue ret;
  uint32_t i = 0;

  ret = JS_NewArray(ctx);

  list_for_each(el, &minnet_sockets) {
    struct wsi_opaque_user_data* session = list_entry(el, struct wsi_opaque_user_data, link);
    // printf("%s @%u #%i %p\n", __func__, i, session->serial, session);

    JS_SetPropertyUint32(ctx, ret, i++, session_object(session, ctx));
  }
  return ret;
}

void
minnet_handlers(JSContext* ctx, struct lws* wsi, struct lws_pollargs args, JSValue out[2]) {
  JSValue func;
  int events = args.events & (POLLIN | POLLOUT);
  // struct wsi_opaque_user_data*opaque =lws_opaque(wsi, ctx);

  if(events)
    func = make_handler(ctx, args.fd, events, wsi);

  out[0] = (events & POLLIN) ? js_function_bind_1(ctx, func, JS_NewInt32(ctx, READ_HANDLER)) : JS_NULL;
  out[1] = (events & POLLOUT) ? js_function_bind_1(ctx, func, JS_NewInt32(ctx, WRITE_HANDLER)) : JS_NULL;

  if(events)
    JS_FreeValue(ctx, func);
}

JSValue
minnet_emit_this(const struct ws_callback* cb, JSValueConst this_obj, int argc, JSValue* argv) {
  JSValue ret = JS_UNDEFINED;

  if(cb->ctx) {
    /*size_t len;
    const char* str  = JS_ToCStringLen(cb->ctx, &len, cb->func_obj);
    // printf("emit %s [%d] \"%.*s\"\n", cb->name, argc, (int)((const char*)memchr(str, '{', len) - str), str);
    JS_FreeCString(cb->ctx, str);*/

    ret = JS_Call(cb->ctx, cb->func_obj, this_obj, argc, argv);
  }

  if(JS_IsException(ret)) {
    JSValue exception = JS_GetException(cb->ctx);
    js_error_print(cb->ctx, exception);
    ret = JS_Throw(cb->ctx, exception);
  }
  /*if(JS_IsException(ret))
    minnet_exception = TRUE; */

  return ret;
}

JSValue
minnet_emit(const struct ws_callback* cb, int argc, JSValue* argv) {
  return minnet_emit_this(cb, cb->this_obj /* ? *cb->this_obj : JS_NULL*/, argc, argv);
}

static const JSCFunctionListEntry minnet_loglevels[] = {
    JS_INDEX_STRING_DEF(1, "ERR"),
    JS_INDEX_STRING_DEF(2, "WARN"),
    JS_INDEX_STRING_DEF(4, "NOTICE"),
    JS_INDEX_STRING_DEF(8, "INFO"),
    JS_INDEX_STRING_DEF(16, "DEBUG"),
    JS_INDEX_STRING_DEF(32, "PARSER"),
    JS_INDEX_STRING_DEF(64, "HEADER"),
    JS_INDEX_STRING_DEF(128, "EXT"),
    JS_INDEX_STRING_DEF(256, "CLIENT"),
    JS_INDEX_STRING_DEF(512, "LATENCY"),
    JS_INDEX_STRING_DEF(1024, "USER"),
    JS_INDEX_STRING_DEF(2048, "THREAD"),
    JS_INDEX_STRING_DEF(4095, "ALL"),
};

static const JSCFunctionListEntry minnet_funcs[] = {
    JS_CFUNC_DEF("server", 1, minnet_server),
    JS_CFUNC_DEF("client", 1, minnet_client),
    JS_CFUNC_DEF("fetch", 1, minnet_fetch),
    JS_CFUNC_SPECIAL_DEF("socket", 1, constructor, minnet_ws_constructor),
    JS_CFUNC_SPECIAL_DEF("url", 1, constructor, minnet_url_constructor),
    // JS_CGETSET_DEF("log", get_log, set_log),
    // JS_CGETSET_DEF("sessions", minnet_get_sessions, 0),
    JS_CFUNC_DEF("getSessions", 0, minnet_get_sessions),
    JS_CFUNC_DEF("setLog", 1, minnet_set_log),
    JS_PROP_INT32_DEF("METHOD_GET", METHOD_GET, 0),
    JS_PROP_INT32_DEF("METHOD_POST", METHOD_POST, 0),
    JS_PROP_INT32_DEF("METHOD_OPTIONS", METHOD_OPTIONS, 0),
    JS_PROP_INT32_DEF("METHOD_PUT", METHOD_PUT, 0),
    JS_PROP_INT32_DEF("METHOD_PATCH", METHOD_PATCH, 0),
    JS_PROP_INT32_DEF("METHOD_DELETE", METHOD_DELETE, 0),
    JS_PROP_INT32_DEF("METHOD_HEAD", METHOD_HEAD, 0),

    JS_PROP_INT32_DEF("LLL_ERR", LLL_ERR, 0),
    JS_PROP_INT32_DEF("LLL_WARN", LLL_WARN, 0),
    JS_PROP_INT32_DEF("LLL_NOTICE", LLL_NOTICE, 0),
    JS_PROP_INT32_DEF("LLL_INFO", LLL_INFO, 0),
    JS_PROP_INT32_DEF("LLL_DEBUG", LLL_DEBUG, 0),
    JS_PROP_INT32_DEF("LLL_PARSER", LLL_PARSER, 0),
    JS_PROP_INT32_DEF("LLL_HEADER", LLL_HEADER, 0),
    JS_PROP_INT32_DEF("LLL_EXT", LLL_EXT, 0),
    JS_PROP_INT32_DEF("LLL_CLIENT", LLL_CLIENT, 0),
    JS_PROP_INT32_DEF("LLL_LATENCY", LLL_LATENCY, 0),
    JS_PROP_INT32_DEF("LLL_USER", LLL_USER, 0),
    JS_PROP_INT32_DEF("LLL_THREAD", LLL_THREAD, 0),
    JS_PROP_INT32_DEF("LLL_ALL", ~((~0u) << LLL_COUNT), 0),
    JS_OBJECT_DEF("logLevels", minnet_loglevels, countof(minnet_loglevels), JS_PROP_CONFIGURABLE),
};

void
value_dump(JSContext* ctx, const char* n, JSValueConst const* v) {
  const char* str = JS_ToCString(ctx, *v);
  lwsl_user("%s = '%s'\n", n, str);
  JS_FreeCString(ctx, str);
}

static int
js_minnet_init(JSContext* ctx, JSModuleDef* m) {
  /*  minnet_log_cb = JS_UNDEFINED;
    minnet_log_this = JS_UNDEFINED;*/

  init_list_head(&minnet_sockets);

  JS_SetModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));

  // Add class Response
  JS_NewClassID(&minnet_response_class_id);
  JS_NewClass(JS_GetRuntime(ctx), minnet_response_class_id, &minnet_response_class);

  minnet_response_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_response_proto, minnet_response_proto_funcs, minnet_response_proto_funcs_size);
  JS_SetClassProto(ctx, minnet_response_class_id, minnet_response_proto);

  minnet_response_ctor = JS_NewCFunction2(ctx, minnet_response_constructor, "MinnetResponse", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, minnet_response_ctor, minnet_response_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Response", minnet_response_ctor);

  // Add class Request
  JS_NewClassID(&minnet_request_class_id);

  JS_NewClass(JS_GetRuntime(ctx), minnet_request_class_id, &minnet_request_class);
  minnet_request_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_request_proto, minnet_request_proto_funcs, minnet_request_proto_funcs_size);
  JS_SetClassProto(ctx, minnet_request_class_id, minnet_request_proto);

  minnet_request_ctor = JS_NewCFunction2(ctx, minnet_request_constructor, "MinnetRequest", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, minnet_request_ctor, minnet_request_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Request", minnet_request_ctor);

  // Add class Ringbuffer
  JS_NewClassID(&minnet_ringbuffer_class_id);

  JS_NewClass(JS_GetRuntime(ctx), minnet_ringbuffer_class_id, &minnet_ringbuffer_class);
  minnet_ringbuffer_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_ringbuffer_proto, minnet_ringbuffer_proto_funcs, minnet_ringbuffer_proto_funcs_size);
  JS_SetClassProto(ctx, minnet_ringbuffer_class_id, minnet_ringbuffer_proto);

  minnet_ringbuffer_ctor = JS_NewCFunction2(ctx, minnet_ringbuffer_constructor, "MinnetRingbuffer", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, minnet_ringbuffer_ctor, minnet_ringbuffer_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Ringbuffer", minnet_ringbuffer_ctor);

  // Add class Generator
  /* JS_NewClassID(&minnet_generator_class_id);

   JS_NewClass(JS_GetRuntime(ctx), minnet_generator_class_id, &minnet_generator_class);
   minnet_generator_proto = JS_NewObject(ctx);
   JS_SetPropertyFunctionList(ctx, minnet_generator_proto, minnet_generator_proto_funcs, minnet_generator_proto_funcs_size);
   JS_SetClassProto(ctx, minnet_generator_class_id, minnet_generator_proto);

   minnet_generator_ctor = JS_NewCFunction2(ctx, minnet_generator_constructor, "MinnetGenerator", 0, JS_CFUNC_constructor, 0);
   JS_SetConstructor(ctx, minnet_generator_ctor, minnet_generator_proto);

   if(m)
     JS_SetModuleExport(ctx, m, "Generator", minnet_generator_ctor);*/

  // Add class URL
  minnet_url_init(ctx, m);

  // Add class WebSocket
  JS_NewClassID(&minnet_ws_class_id);
  JS_NewClass(JS_GetRuntime(ctx), minnet_ws_class_id, &minnet_ws_class);
  minnet_ws_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_ws_proto, minnet_ws_proto_funcs, minnet_ws_proto_funcs_size);
  JS_SetPropertyFunctionList(ctx, minnet_ws_proto, minnet_ws_proto_defs, minnet_ws_proto_defs_size);

  minnet_ws_ctor = JS_NewCFunction2(ctx, minnet_ws_constructor, "MinnetWebsocket", 0, JS_CFUNC_constructor, 0);
  JS_SetPropertyFunctionList(ctx, minnet_ws_ctor, minnet_ws_static_funcs, minnet_ws_static_funcs_size);

  JS_SetConstructor(ctx, minnet_ws_ctor, minnet_ws_proto);

  JS_SetPropertyFunctionList(ctx, minnet_ws_ctor, minnet_ws_proto_defs, minnet_ws_proto_defs_size);

  if(m)
    JS_SetModuleExport(ctx, m, "Socket", minnet_ws_ctor);

  // Add class FormParser
  JS_NewClassID(&minnet_form_parser_class_id);

  JS_NewClass(JS_GetRuntime(ctx), minnet_form_parser_class_id, &minnet_form_parser_class);
  minnet_form_parser_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_form_parser_proto, minnet_form_parser_proto_funcs, minnet_form_parser_proto_funcs_size);
  JS_SetClassProto(ctx, minnet_form_parser_class_id, minnet_form_parser_proto);

  minnet_form_parser_ctor = JS_NewCFunction2(ctx, minnet_form_parser_constructor, "MinnetFormParser", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, minnet_form_parser_ctor, minnet_form_parser_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "FormParser", minnet_form_parser_ctor);

  // Add class Hash
  JS_NewClassID(&minnet_hash_class_id);

  JS_NewClass(JS_GetRuntime(ctx), minnet_hash_class_id, &minnet_hash_class);
  minnet_hash_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_hash_proto, minnet_hash_proto_funcs, minnet_hash_proto_funcs_size);
  JS_SetClassProto(ctx, minnet_hash_class_id, minnet_hash_proto);

  minnet_hash_ctor = JS_NewCFunction2(ctx, minnet_hash_constructor, "MinnetHash", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, minnet_hash_ctor, minnet_hash_proto);
  JS_SetPropertyFunctionList(ctx, minnet_hash_ctor, minnet_hash_static_funcs, minnet_hash_static_funcs_size);

  if(m)
    JS_SetModuleExport(ctx, m, "Hash", minnet_hash_ctor);

  {
    JSValue minnet_default = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, minnet_default, minnet_funcs, countof(minnet_funcs));
    JS_SetModuleExport(ctx, m, "default", minnet_default);
  }

  return 0;
}

__attribute__((visibility("default"))) JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_minnet_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "Response");
  JS_AddModuleExport(ctx, m, "Request");
  JS_AddModuleExport(ctx, m, "Ringbuffer");
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

char*
lws_get_peer(struct lws* wsi, JSContext* ctx) {
  char buf[1024];

  lws_get_peer_simple(wsi, buf, sizeof(buf) - 1);

  return js_strdup(ctx, buf);
}

char*
lws_get_host(struct lws* wsi, JSContext* ctx) {
  return lws_get_token(wsi, ctx, lws_wsi_is_h2(wsi) ? WSI_TOKEN_HTTP_COLON_AUTHORITY : WSI_TOKEN_HOST);
}

void
lws_peer_cert(struct lws* wsi) {
  uint8_t buf[1280];
  union lws_tls_cert_info_results* ci = (union lws_tls_cert_info_results*)buf;
#if defined(LWS_HAVE_CTIME_R)
  char date[32];
#endif

  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_COMMON_NAME, ci, sizeof(buf) - sizeof(*ci)))
    lwsl_notice(" Peer Cert CN        : %s\n", ci->ns.name);

  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_ISSUER_NAME, ci, sizeof(ci->ns.name)))
    lwsl_notice(" Peer Cert issuer    : %s\n", ci->ns.name);

#if defined(LWS_HAVE_CTIME_R)
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_VALIDITY_FROM, ci, 0))
    lwsl_notice(" Peer Cert Valid from: %s", ctime_r(&ci->time, date));
#else
  lwsl_notice(" Peer Cert Valid from: %s", ctime(&ci->time));
#endif
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_VALIDITY_TO, ci, 0))
#if defined(LWS_HAVE_CTIME_R)
    lwsl_notice(" Peer Cert Valid to  : %s", ctime_r(&ci->time, date));
#else
    lwsl_notice(" Peer Cert Valid to  : %s", ctime(&ci->time));
#endif
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_USAGE, ci, 0))
    lwsl_notice(" Peer Cert usage bits: 0x%x\n", ci->usage);
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_OPAQUE_PUBLIC_KEY, ci, sizeof(buf) - sizeof(*ci))) {
    lwsl_notice(" Peer Cert public key:\n");
    lwsl_hexdump_notice(ci->ns.name, (unsigned int)ci->ns.len);
  }

  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_AUTHORITY_KEY_ID, ci, 0)) {
    lwsl_notice(" AUTHORITY_KEY_ID\n");
    lwsl_hexdump_notice(ci->ns.name, (size_t)ci->ns.len);
  }
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_AUTHORITY_KEY_ID_ISSUER, ci, 0)) {
    lwsl_notice(" AUTHORITY_KEY_ID ISSUER\n");
    lwsl_hexdump_notice(ci->ns.name, (size_t)ci->ns.len);
  }
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_AUTHORITY_KEY_ID_SERIAL, ci, 0)) {
    lwsl_notice(" AUTHORITY_KEY_ID SERIAL\n");
    lwsl_hexdump_notice(ci->ns.name, (size_t)ci->ns.len);
  }
  if(!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_SUBJECT_KEY_ID, ci, 0)) {
    lwsl_notice(" AUTHORITY_KEY_ID SUBJECT_KEY_ID\n");
    lwsl_hexdump_notice(ci->ns.name, (size_t)ci->ns.len);
  }
}

char*
fd_address(int fd, int (*fn)(int, struct sockaddr*, socklen_t*)) {
  const char* s = 0;
  union {
    struct sockaddr a;
    struct sockaddr_in ai;
    struct sockaddr_in6 ai6;
  } sa;
  socklen_t sl = sizeof(s);
  uint16_t port;
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
char*
fd_remote(int fd) {
  return fd_address(fd, &getpeername);
}
char*
fd_local(int fd) {
  return fd_address(fd, &getsockname);
}

const char*
lws_callback_name(int reason) {
  return ((const char* const[]){
      "LWS_CALLBACK_ESTABLISHED",
      "LWS_CALLBACK_CLIENT_CONNECTION_ERROR",
      "LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH",
      "LWS_CALLBACK_CLIENT_ESTABLISHED",
      "LWS_CALLBACK_CLOSED",
      "LWS_CALLBACK_CLOSED_HTTP",
      "LWS_CALLBACK_RECEIVE",
      "LWS_CALLBACK_RECEIVE_PONG",
      "LWS_CALLBACK_CLIENT_RECEIVE",
      "LWS_CALLBACK_CLIENT_RECEIVE_PONG",
      "LWS_CALLBACK_CLIENT_WRITEABLE",
      "LWS_CALLBACK_SERVER_WRITEABLE",
      "LWS_CALLBACK_HTTP",
      "LWS_CALLBACK_HTTP_BODY",
      "LWS_CALLBACK_HTTP_BODY_COMPLETION",
      "LWS_CALLBACK_HTTP_FILE_COMPLETION",
      "LWS_CALLBACK_HTTP_WRITEABLE",
      "LWS_CALLBACK_FILTER_NETWORK_CONNECTION",
      "LWS_CALLBACK_FILTER_HTTP_CONNECTION",
      "LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED",
      "LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION",
      "LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS",
      "LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS",
      "LWS_CALLBACK_OPENSSL_PERFORM_CLIENT_CERT_VERIFICATION",
      "LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER",
      "LWS_CALLBACK_CONFIRM_EXTENSION_OKAY",
      "LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED",
      "LWS_CALLBACK_PROTOCOL_INIT",
      "LWS_CALLBACK_PROTOCOL_DESTROY",
      "LWS_CALLBACK_WSI_CREATE",
      "LWS_CALLBACK_WSI_DESTROY",
      "LWS_CALLBACK_GET_THREAD_ID",
      "LWS_CALLBACK_ADD_POLL_FD",
      "LWS_CALLBACK_DEL_POLL_FD",
      "LWS_CALLBACK_CHANGE_MODE_POLL_FD",
      "LWS_CALLBACK_LOCK_POLL",
      "LWS_CALLBACK_UNLOCK_POLL",
      "LWS_CALLBACK_OPENSSL_CONTEXT_REQUIRES_PRIVATE_KEY",
      "LWS_CALLBACK_WS_PEER_INITIATED_CLOSE",
      "LWS_CALLBACK_WS_EXT_DEFAULTS",
      "LWS_CALLBACK_CGI",
      "LWS_CALLBACK_CGI_TERMINATED",
      "LWS_CALLBACK_CGI_STDIN_DATA",
      "LWS_CALLBACK_CGI_STDIN_COMPLETED",
      "LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP",
      "LWS_CALLBACK_CLOSED_CLIENT_HTTP",
      "LWS_CALLBACK_RECEIVE_CLIENT_HTTP",
      "LWS_CALLBACK_COMPLETED_CLIENT_HTTP",
      "LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ",
      "LWS_CALLBACK_HTTP_BIND_PROTOCOL",
      "LWS_CALLBACK_HTTP_DROP_PROTOCOL",
      "LWS_CALLBACK_CHECK_ACCESS_RIGHTS",
      "LWS_CALLBACK_PROCESS_HTML",
      "LWS_CALLBACK_ADD_HEADERS",
      "LWS_CALLBACK_SESSION_INFO",
      "LWS_CALLBACK_GS_EVENT",
      "LWS_CALLBACK_HTTP_PMO",
      "LWS_CALLBACK_CLIENT_HTTP_WRITEABLE",
      "LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION",
      "LWS_CALLBACK_RAW_RX",
      "LWS_CALLBACK_RAW_CLOSE",
      "LWS_CALLBACK_RAW_WRITEABLE",
      "LWS_CALLBACK_RAW_ADOPT",
      "LWS_CALLBACK_RAW_ADOPT_FILE",
      "LWS_CALLBACK_RAW_RX_FILE",
      "LWS_CALLBACK_RAW_WRITEABLE_FILE",
      "LWS_CALLBACK_RAW_CLOSE_FILE",
      "LWS_CALLBACK_SSL_INFO",
      0,
      "LWS_CALLBACK_CHILD_CLOSING",
      "LWS_CALLBACK_CGI_PROCESS_ATTACH",
      "LWS_CALLBACK_EVENT_WAIT_CANCELLED",
      "LWS_CALLBACK_VHOST_CERT_AGING",
      "LWS_CALLBACK_TIMER",
      "LWS_CALLBACK_VHOST_CERT_UPDATE",
      "LWS_CALLBACK_CLIENT_CLOSED",
      "LWS_CALLBACK_CLIENT_HTTP_DROP_PROTOCOL",
      "LWS_CALLBACK_WS_SERVER_BIND_PROTOCOL",
      "LWS_CALLBACK_WS_SERVER_DROP_PROTOCOL",
      "LWS_CALLBACK_WS_CLIENT_BIND_PROTOCOL",
      "LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL",
      "LWS_CALLBACK_RAW_SKT_BIND_PROTOCOL",
      "LWS_CALLBACK_RAW_SKT_DROP_PROTOCOL",
      "LWS_CALLBACK_RAW_FILE_BIND_PROTOCOL",
      "LWS_CALLBACK_RAW_FILE_DROP_PROTOCOL",
      "LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL",
      "LWS_CALLBACK_HTTP_CONFIRM_UPGRADE",
      0,
      0,
      "LWS_CALLBACK_RAW_PROXY_CLI_RX",
      "LWS_CALLBACK_RAW_PROXY_SRV_RX",
      "LWS_CALLBACK_RAW_PROXY_CLI_CLOSE",
      "LWS_CALLBACK_RAW_PROXY_SRV_CLOSE",
      "LWS_CALLBACK_RAW_PROXY_CLI_WRITEABLE",
      "LWS_CALLBACK_RAW_PROXY_SRV_WRITEABLE",
      "LWS_CALLBACK_RAW_PROXY_CLI_ADOPT",
      "LWS_CALLBACK_RAW_PROXY_SRV_ADOPT",
      "LWS_CALLBACK_RAW_PROXY_CLI_BIND_PROTOCOL",
      "LWS_CALLBACK_RAW_PROXY_SRV_BIND_PROTOCOL",
      "LWS_CALLBACK_RAW_PROXY_CLI_DROP_PROTOCOL",
      "LWS_CALLBACK_RAW_PROXY_SRV_DROP_PROTOCOL",
      "LWS_CALLBACK_RAW_CONNECTED",
      "LWS_CALLBACK_VERIFY_BASIC_AUTHORIZATION",
      "LWS_CALLBACK_WSI_TX_CREDIT_GET",
      "LWS_CALLBACK_CLIENT_HTTP_REDIRECT",
      "LWS_CALLBACK_CONNECTING",
  })[reason];
}

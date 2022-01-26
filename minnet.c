#include "minnet-server.h"
#include "minnet-client.h"
#include "minnet-request.h"
#include "minnet-response.h"
#include "minnet-websocket.h"
#include "minnet-stream.h"
#include "minnet-url.h"
#include "jsutils.h"
#include "minnet-buffer.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>

#ifdef _WIN32
#include "poll.h"
#endif

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

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

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
THREAD_LOCAL BOOL minnet_exception = FALSE;

static void
lws_log_callback(int level, const char* line) {
  if(minnet_log_ctx) {
    if(JS_IsFunction(minnet_log_ctx, minnet_log_cb)) {
      size_t n = 0, len = strlen(line);

      if(len > 0 && line[0] == '[') {
        if((n = byte_chr(line, len, ']')) < len)
          n++;
        while(n < len && isspace(line[n])) n++;
        if(n + 1 < len && line[n + 1] == ':')
          n += 2;
        while(n < len && (isspace(line[n]) || line[n] == '-')) n++;
      }
      if(len > 0 && line[len - 1] == '\n')
        len--;

      JSValueConst argv[2] = {JS_NewInt32(minnet_log_ctx, level), JS_NewStringLen(minnet_log_ctx, line + n, len - n)};
      JSValue ret = JS_Call(minnet_log_ctx, minnet_log_cb, minnet_log_this, 2, argv);

      if(JS_IsException(ret))
        minnet_exception = TRUE;

      JS_FreeValue(minnet_log_ctx, argv[0]);
      JS_FreeValue(minnet_log_ctx, argv[1]);
      JS_FreeValue(minnet_log_ctx, ret);
    } else {
      js_console_log(minnet_log_ctx, &minnet_log_this, &minnet_log_cb);
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
  lws_set_log_level(((unsigned)minnet_log_level & ((1u << LLL_COUNT) - 1)), lws_log_callback);
  return ret;
}

JSValue
headers_object(JSContext* ctx, const MinnetBuffer* buffer) {
  JSValue ret = JS_NewObject(ctx);
  size_t len, namelen, n;
  uint8_t *x, *end;
  for(x = buffer->start, end = buffer->write; x < end; x += len + 1) {
    len = byte_chr(x, end - x, '\n');
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
headers_from(MinnetBuffer* buffer, struct lws* wsi, JSValueConst obj, JSContext* ctx) {
  JSPropertyEnum* tab;
  uint32_t tab_len, i;

  if(JS_GetOwnPropertyNames(ctx, &tab, &tab_len, obj, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK))
    return 0;

  for(i = 0; i < tab_len; i++) {
    JSValue value = JS_GetProperty(ctx, obj, tab[i].atom);
    size_t len;
    char* prop;
    const char* str;
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

int
fd_callback(struct lws* wsi, enum lws_callback_reasons reason, MinnetCallback* cb, struct lws_pollargs* args) {

  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: return 0;

    case LWS_CALLBACK_ADD_POLL_FD: {

      if(cb->ctx) {
        JSValue argv[3] = {JS_NewInt32(cb->ctx, args->fd)};
        minnet_handlers(cb->ctx, wsi, args, &argv[1]);

        minnet_emit(cb, 3, argv);

        JS_FreeValue(cb->ctx, argv[0]);
        JS_FreeValue(cb->ctx, argv[1]);
        JS_FreeValue(cb->ctx, argv[2]);
      }
      return 0;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {

      if(cb->ctx) {
        JSValue argv[3] = {
            JS_NewInt32(cb->ctx, args->fd),
        };
        minnet_handlers(cb->ctx, wsi, args, &argv[1]);
        minnet_emit(cb, 3, argv);
        JS_FreeValue(cb->ctx, argv[0]);
        JS_FreeValue(cb->ctx, argv[1]);
        JS_FreeValue(cb->ctx, argv[2]);
      }
      return 0;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {

      if(cb->ctx) {
        if(args->events != args->prev_events) {
          JSValue argv[3] = {JS_NewInt32(cb->ctx, args->fd)};
          minnet_handlers(cb->ctx, wsi, args, &argv[1]);

          minnet_emit(cb, 3, argv);
          JS_FreeValue(cb->ctx, argv[0]);
          JS_FreeValue(cb->ctx, argv[1]);
          JS_FreeValue(cb->ctx, argv[2]);
        }
      }
      return 0;
    }

    default: {
      return -1;
    }
  }
}

static const char*
io_events(int events) {
  switch(events /* & (POLLIN | POLLOUT)*/) {
    case POLLOUT | POLLHUP: return "OUT|HUP";
    case POLLIN | POLLOUT | POLLHUP | POLLERR: return "IN|OUT|HUP|ERR";
    case POLLOUT | POLLHUP | POLLERR: return "OUT|HUP|ERR";
    case POLLIN | POLLOUT: return "IN|OUT";
    case POLLIN: return "IN";
    case POLLOUT:
      return "OUT";
      //  case 0: return "0";
  }
  assert(!events);
  return "";
}

static int
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
}

static JSValue
minnet_io_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  struct handler_closure* closure = ptr;
  struct pollfd* p;
  int32_t wr, ret;

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

    ret = lws_service_fd(lws_get_context(closure->lwsi), &x);
  }

  return JS_UNDEFINED;
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

/*void
minnet_handlers(JSContext* ctx, struct lws* wsi, struct lws_pollargs* args, JSValue out[2]) {
  struct wsi_opaque_user_data* opaque = lws_opaque(wsi, ctx);
  int events = args->events & (POLLIN | POLLOUT);
  assert(opaque);

  if(!JS_IsObject(opaque->handlers[READ_HANDLER]) || !JS_IsObject(opaque->handlers[WRITE_HANDLER])) {
    JSValue func = make_handler(ctx, args->fd, events, wsi);

    for(int i = READ_HANDLER; i <= WRITE_HANDLER; i++) opaque->handlers[i] = js_function_bind_1(ctx, func, JS_NewInt32(ctx, i));
    JS_FreeValue(ctx, func);
  }

  opaque->poll = (struct pollfd){args->fd, events, 0};

  out[0] = (events & POLLIN) ? opaque->handlers[READ_HANDLER] : JS_NULL;
  out[1] = (events & POLLOUT) ? opaque->handlers[WRITE_HANDLER] : JS_NULL;
}
*/
void
minnet_handlers(JSContext* ctx, struct lws* wsi, struct lws_pollargs* args, JSValue out[2]) {
  JSValue func;
  int events = args->events & (POLLIN | POLLOUT);
  // struct wsi_opaque_user_data*opaque =lws_opaque(wsi, ctx);

  if(events)
    func = make_handler(ctx, args->fd, events, wsi);

  out[0] = (events & POLLIN) ? js_function_bind_1(ctx, func, JS_NewInt32(ctx, READ_HANDLER)) : JS_NULL;
  out[1] = (events & POLLOUT) ? js_function_bind_1(ctx, func, JS_NewInt32(ctx, WRITE_HANDLER)) : JS_NULL;

  if(events)
    JS_FreeValue(ctx, func);
}

JSValue
minnet_emit_this(const struct ws_callback* cb, JSValueConst this_obj, int argc, JSValue* argv) {
  JSValue ret = JS_UNDEFINED;

  if(cb->ctx) {
    size_t len;
    const char* str;
    str = JS_ToCStringLen(cb->ctx, &len, cb->func_obj);
    // printf("emit %s [%d] \"%.*s\"\n", cb->name, argc, (int)((const char*)memchr(str, '{', len) - str), str);
    JS_FreeCString(cb->ctx, str);

    ret = JS_Call(cb->ctx, cb->func_obj, this_obj, argc, argv);
  }

  if(JS_IsException(ret))
    minnet_exception = TRUE;

  return ret;
}

JSValue
minnet_emit(const struct ws_callback* cb, int argc, JSValue* argv) {
  return minnet_emit_this(cb, cb->this_obj /* ? *cb->this_obj : JS_NULL*/, argc, argv);
}

static const JSCFunctionListEntry minnet_funcs[] = {
    JS_CFUNC_DEF("server", 1, minnet_ws_server),
    JS_CFUNC_DEF("client", 1, minnet_ws_client),
    JS_CFUNC_DEF("fetch", 1, minnet_fetch),
    JS_CFUNC_SPECIAL_DEF("socket", 1, constructor, minnet_ws_constructor),
    JS_CFUNC_SPECIAL_DEF("url", 1, constructor, minnet_url_constructor),
    // JS_CGETSET_DEF("log", get_log, set_log),
    JS_CFUNC_DEF("setLog", 1, minnet_set_log),
    JS_PROP_INT32_DEF("METHOD_GET", METHOD_GET, 0),
    JS_PROP_INT32_DEF("METHOD_POST", METHOD_POST, 0),
    JS_PROP_INT32_DEF("METHOD_OPTIONS", METHOD_OPTIONS, 0),
    JS_PROP_INT32_DEF("METHOD_PUT", METHOD_PUT, 0),
    JS_PROP_INT32_DEF("METHOD_PATCH", METHOD_PATCH, 0),
    JS_PROP_INT32_DEF("METHOD_DELETE", METHOD_DELETE, 0),
    JS_PROP_INT32_DEF("METHOD_CONNECT", METHOD_CONNECT, 0),
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

  // Add class Stream
  JS_NewClassID(&minnet_stream_class_id);

  JS_NewClass(JS_GetRuntime(ctx), minnet_stream_class_id, &minnet_stream_class);
  minnet_stream_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_stream_proto, minnet_stream_proto_funcs, minnet_stream_proto_funcs_size);
  JS_SetClassProto(ctx, minnet_stream_class_id, minnet_stream_proto);

  minnet_stream_ctor = JS_NewCFunction2(ctx, minnet_stream_constructor, "MinnetStream", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, minnet_stream_ctor, minnet_stream_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Stream", minnet_stream_ctor);

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
  JS_AddModuleExport(ctx, m, "Stream");
  JS_AddModuleExport(ctx, m, "Socket");
  JS_AddModuleExport(ctx, m, "URL");
  JS_AddModuleExport(ctx, m, "default");
  JS_AddModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));

  minnet_log_ctx = ctx;

  lws_set_log_level(minnet_log_level, lws_log_callback);

  return m;
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

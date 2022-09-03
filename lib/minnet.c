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
#include "minnet-fetch.h"
#include "jsutils.h"
#include "utils.h"
#include "minnet-buffer.h"
#include <libwebsockets.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/time.h>

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

typedef struct handler_closure {
  JSContext* ctx;
  struct lws* lwsi;
  struct wsi_opaque_user_data* opaque;
} MinnetHandler;

static THREAD_LOCAL JSValue minnet_log_cb, minnet_log_this;
static THREAD_LOCAL int32_t minnet_log_level = 0;
static THREAD_LOCAL JSContext* minnet_log_ctx = 0;
THREAD_LOCAL struct list_head minnet_sockets = {0, 0};

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

JSValue
context_exception(MinnetContext* context, JSValue retval) {
  if(JS_IsException(retval)) {
    context->exception = TRUE;
    JSValue exception = JS_GetException(context->js);
    JSValue stack = JS_GetPropertyStr(context->js, exception, "stack");
    const char* err = JS_ToCString(context->js, exception);
    const char* stk = JS_ToCString(context->js, stack);
    printf("Got exception: %s\n%s\n", err, stk);
    JS_FreeCString(context->js, err);
    JS_FreeCString(context->js, stk);
    JS_FreeValue(context->js, stack);
    JS_Throw(context->js, exception);
  }

  return retval;
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

int
minnet_lws_unhandled(const char* handler, int reason) {
  lwsl_warn("Unhandled \x1b[1;31m%s\x1b[0m event: %i %s\n", handler, reason, lws_callback_name(reason));
  assert(0);
  return -1;
}

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

static JSValue
minnet_io_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  MinnetHandler* closure = ptr;
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

    /*if(x.revents & POLLOUT)
      if(x.revents & POLLIN)
        x.revents &= ~(POLLOUT);*/
    // errno = 0;

    ret = JS_NewInt32(ctx, lws_service_fd(lws_get_context(closure->lwsi), &x));
  }

  return ret;
}

static void
free_handler_closure(void* ptr) {
  MinnetHandler* closure = ptr;
  JSContext* ctx = closure->ctx;
  js_free(ctx, closure);
};

static JSValue
make_handler(JSContext* ctx, int fd, int events, struct lws* wsi) {
  MinnetHandler* closure;

  if(!(closure = js_mallocz(ctx, sizeof(MinnetHandler))))
    return JS_ThrowOutOfMemory(ctx);

  *closure = (MinnetHandler){ctx, wsi, lws_opaque(wsi, ctx)};

  closure->opaque->poll = (struct pollfd){fd, events, 0};

  return JS_NewCClosure(ctx, minnet_io_handler, 1, events, closure, free_handler_closure);
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

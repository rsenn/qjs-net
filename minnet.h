#include "cutils.h"
#include "quickjs.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <libwebsockets.h>

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

#define countof(x) (sizeof(x) / sizeof((x)[0]))
#define JS_CGETSET_MAGIC_FLAGS_DEF(prop_name, fgetter, fsetter, magic_num, flags)                                              \
  {                                                                                                                            \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET_MAGIC, .magic = magic_num, .u = {                       \
      .getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}}                                           \
    }                                                                                                                          \
  }

#define GETCB(opt, cb_ptr)                                                                                                     \
  if(JS_IsFunction(ctx, opt)) {                                                                                                \
    struct minnet_ws_callback cb = {ctx, &this_val, &opt};                                                                     \
    cb_ptr = cb;                                                                                                               \
  }
#define SETLOG lws_set_log_level(LLL_ERR, NULL);

typedef struct minnet_ws_callback {
  JSContext* ctx;
  JSValueConst* this_obj;
  JSValue* func_obj;
} minnet_ws_callback;

extern JSValue minnet_log, minnet_log_this;
extern JSContext* minnet_log_ctx;
extern BOOL minnet_exception;
extern JSClassID minnet_ws_class_id;

/* class WebSocket */

typedef struct {
  struct lws* lwsi;
  size_t ref_count;
  struct http_header* header;
} MinnetWebsocket;

static void minnet_ws_finalizer(JSRuntime* rt, JSValue val);

static JSClassDef minnet_ws_class = {
    "MinnetWebSocket",
    .finalizer = minnet_ws_finalizer,
};

/* class Response */

typedef struct {
  uint8_t* buffer;
  long size;
  JSValue status;
  JSValue ok;
  JSValue url;
  JSValue type;
} MinnetResponse;

static JSValue minnet_response_buffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
static JSValue minnet_response_json(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
static JSValue minnet_response_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
static JSValue minnet_response_getter_ok(JSContext* ctx, JSValueConst this_val);
static JSValue minnet_response_getter_url(JSContext* ctx, JSValueConst this_val);
static JSValue minnet_response_getter_status(JSContext* ctx, JSValueConst this_val);
static JSValue minnet_response_getter_type(JSContext* ctx, JSValueConst this_val);
static void minnet_response_finalizer(JSRuntime* rt, JSValue val);

static JSClassDef minnet_response_class = {
    "MinnetResponse",
    .finalizer = minnet_response_finalizer,
};

static const JSCFunctionListEntry minnet_response_proto_funcs[] = {
    JS_CFUNC_DEF("arrayBuffer", 0, minnet_response_buffer),
    JS_CFUNC_DEF("json", 0, minnet_response_json),
    JS_CFUNC_DEF("text", 0, minnet_response_text),
    JS_CGETSET_DEF("ok", minnet_response_getter_ok, NULL),
    JS_CGETSET_DEF("url", minnet_response_getter_url, NULL),
    JS_CGETSET_DEF("status", minnet_response_getter_status, NULL),
    JS_CGETSET_DEF("type", minnet_response_getter_type, NULL),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetResponse", JS_PROP_CONFIGURABLE),
};

static JSClassID minnet_response_class_id;

void minnet_ws_sslcert(JSContext* ctx, struct lws_context_creation_info* info, JSValueConst options);

#include "client.h"

JSValue minnet_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

enum { READ_HANDLER = 0, WRITE_HANDLER };

JSValue
minnet_service_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data);

static inline JSValue
minnet_make_handler(JSContext* ctx, struct lws_pollargs* pfd, struct lws* wsi, int magic) {
  JSValue data[5] = {
      JS_MKVAL(JS_TAG_INT, pfd->fd),
      JS_MKVAL(JS_TAG_INT, pfd->events),
      JS_MKPTR(0, lws_get_context(wsi)),
      JS_MKVAL(JS_TAG_INT, 0),
      JS_MKPTR(0, *(void**)pfd),
  };

  return JS_NewCFunctionData(ctx, minnet_service_handler, 0, magic, countof(data), data);
}

static inline JSValue
minnet_function_bound(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue* func_data) {
  JSValue args[argc + magic];
  size_t i, j;
  for(i = 0; i < magic; i++) args[i] = func_data[i + 1];
  for(j = 0; j < argc; j++) args[i++] = argv[j];

  return JS_Call(ctx, func_data[0], this_val, i, args);
}

static inline JSValue
minnet_function_bind(JSContext* ctx, JSValueConst func, int argc, JSValueConst argv[]) {
  JSValue data[argc + 1];
  size_t i;
  data[0] = JS_DupValue(ctx, func);
  for(i = 0; i < argc; i++) data[i + 1] = JS_DupValue(ctx, argv[i]);
  return JS_NewCFunctionData(ctx, minnet_function_bound, 0, argc, argc + 1, data);
}

static inline JSValue
minnet_function_bind_1(JSContext* ctx, JSValueConst func, JSValueConst arg) {
  return minnet_function_bind(ctx, func, 1, &arg);
}

static inline void
minnet_make_handlers(JSContext* ctx, struct lws* wsi, struct lws_pollargs* pfd, JSValue out[2]) {
  JSValue func = minnet_make_handler(ctx, pfd, wsi, 0);

  out[0] = (pfd->events & POLLIN) ? minnet_function_bind_1(ctx, func, JS_NewInt32(ctx, READ_HANDLER)) : JS_NULL;
  out[1] = (pfd->events & POLLOUT) ? minnet_function_bind_1(ctx, func, JS_NewInt32(ctx, WRITE_HANDLER)) : JS_NULL;

  JS_FreeValue(ctx, func);
}

#define GETCB(opt, cb_ptr)                                                                                                     \
  if(JS_IsFunction(ctx, opt)) {                                                                                                \
    struct minnet_ws_callback cb = {ctx, &this_val, &opt};                                                                     \
    cb_ptr = cb;                                                                                                               \
  }
#define SETLOG lws_set_log_level(LLL_ERR, NULL);

static inline void
get_console_log(JSContext* ctx, JSValue* console, JSValue* console_log) {
  JSValue global = JS_GetGlobalObject(ctx);
  *console = JS_GetPropertyStr(ctx, global, "console");
  *console_log = JS_GetPropertyStr(ctx, *console, "log");
  JS_FreeValue(ctx, global);
}

static inline JSValue
minnet_get_log(JSContext* ctx, JSValueConst this_val) {
  return JS_DupValue(ctx, minnet_log);
}

static inline JSValue
minnet_set_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = minnet_log;

  minnet_log_ctx = ctx;
  minnet_log = JS_DupValue(ctx, argv[0]);
  if(argc > 1) {
    JS_FreeValue(ctx, minnet_log_this);
    minnet_log_this = JS_DupValue(ctx, argv[1]);
  }
  return ret;
}

static inline JSValue
call_ws_callback(minnet_ws_callback* cb, int argc, JSValue* argv) {
  if(!cb->func_obj)
    return JS_UNDEFINED;
  return JS_Call(cb->ctx, *(cb->func_obj), *(cb->this_obj), argc, argv);
}

static inline JSValue
create_websocket_obj(JSContext* ctx, struct lws* wsi) {
  MinnetWebsocket* res;
  JSValue ws_obj = JS_NewObjectClass(ctx, minnet_ws_class_id);

  if(JS_IsException(ws_obj))
    return JS_EXCEPTION;

  if(!(res = js_mallocz(ctx, sizeof(*res)))) {
    JS_FreeValue(ctx, ws_obj);
    return JS_EXCEPTION;
  }

  res->lwsi = wsi;
  res->ref_count = 1;

  JS_SetOpaque(ws_obj, res);

  lws_set_wsi_user(wsi, JS_VALUE_GET_OBJ(JS_DupValue(ctx, ws_obj)));

  return ws_obj;
}

static inline JSValue
get_websocket_obj(JSContext* ctx, struct lws* wsi) {
  JSObject* obj;

  if((obj = lws_wsi_user(wsi))) {
    JSValue ws_obj = JS_MKPTR(JS_TAG_OBJECT, obj);
    MinnetWebsocket* res = JS_GetOpaque2(ctx, ws_obj, minnet_ws_class_id);

    res->ref_count++;

    return JS_DupValue(ctx, ws_obj);
  }

  return create_websocket_obj(ctx, wsi);
}

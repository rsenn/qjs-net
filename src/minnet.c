#define _GNU_SOURCE
#include "minnet-server.h"
#include "minnet-client.h"
#include "minnet-request.h"
#include "minnet-response.h"
#include "minnet-websocket.h"
#include "minnet-ringbuffer.h"
#include "minnet-generator.h"
#include "minnet-asynciterator.h"
#include "minnet-formparser.h"
#include "minnet-hash.h"
#include "minnet-fetch.h"
#include "minnet-headers.h"
#include "js-utils.h"
#include "utils.h"
#include "buffer.h"
#include <libwebsockets.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/time.h>
#include <stdarg.h>

/*#ifdef _WIN32
#include "poll.h"
#endif*/

static THREAD_LOCAL JSValue minnet_log_cb, minnet_log_this;
static THREAD_LOCAL int32_t minnet_log_level = 0;
static THREAD_LOCAL JSContext* minnet_log_ctx = 0;
struct lws_protocols *minnet_client_protocols = 0, *minnet_server_protocols = 0;

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

#define PIO (POLLIN | POLLOUT | POLLERR)

typedef enum { READ_HANDLER = 0, WRITE_HANDLER } JSIOHandler;

#ifdef _WIN32
static THREAD_LOCAL intptr_t* osfhandle_map;
static THREAD_LOCAL size_t osfhandle_count;

static int
make_osf_handle(intptr_t handle) {
  int ret;

  assert((HANDLE)handle != INVALID_HANDLE_VALUE);

  ret = _open_osfhandle((SOCKET)handle, 0);

  if(ret >= osfhandle_count) {
    size_t oldsize = osfhandle_count;

    osfhandle_count = ret + 1;
    osfhandle_map = realloc(osfhandle_map, sizeof(intptr_t) * osfhandle_count);
    assert(osfhandle_map);
    memset(&osfhandle_map[oldsize], 0, (osfhandle_count - oldsize) * sizeof(intptr_t));
  }

  osfhandle_map[ret] = handle;

  return ret;
};

static int
get_osf_handle(intptr_t handle) {
  assert((HANDLE)handle != INVALID_HANDLE_VALUE);

  if(osfhandle_map)
    for(size_t i = 0; i < osfhandle_count; i++)
      if(osfhandle_map[i] == handle)
        return i;

  return make_osf_handle(handle);
}

static void
close_osf_handle(int fd) {
  intptr_t handle = (intptr_t)_get_osfhandle(fd);

  assert(fd + 1 <= osfhandle_count);
  assert(osfhandle_map);
  assert((HANDLE)handle != INVALID_HANDLE_VALUE);
  assert(handle == osfhandle_map[fd]);

  close(fd);

  if(osfhandle_count == fd + 1) {
    --osfhandle_count;
    osfhandle_map = realloc(osfhandle_map, sizeof(intptr_t) * osfhandle_count);
  }
}
#else
#define get_osf_handle(fd) (fd)
#endif

typedef struct {
  JSContext* ctx;
  struct lws* lwsi;
  struct wsi_opaque_user_data* opaque;
} IOHandlerClosure;

static void
lws_iohandler_free(void* ptr) {
  IOHandlerClosure* closure = ptr;

  js_free(closure->ctx, closure);
};

static JSValue
lws_iohandler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  IOHandlerClosure* closure = ptr;
  struct pollfd* p;
  JSIOHandler wr = JS_ToBool(ctx, argv[0]);
  JSValue ret = JS_UNDEFINED;
  int events = (wr ? POLLOUT : POLLIN);

  assert(closure->opaque);
  p = &closure->opaque->poll;

  p->revents = events;

  /*if((p->revents & PIO) != magic)
    if(poll(p, 1, 0) < 0)
      lwsl_err("poll error: %s\n", strerror(errno));

  if(p->revents & PIO)*/
  {
    struct lws_pollfd x = {p->fd, events, p->revents & PIO};

#ifdef _WIN32
    x.fd = (SOCKET)_get_osfhandle(p->fd);
#endif

    /* if(p->revents & (POLLERR | POLLHUP))
       closure->opaque->poll = *p;*/

    ret = JS_NewInt32(ctx, lws_service_fd(lws_get_context(closure->lwsi), &x));
  }

  return ret;
}

static JSValue
minnet_io_handler(JSContext* ctx, struct lws* wsi) {
  IOHandlerClosure* h;

  if(!(h = js_mallocz(ctx, sizeof(IOHandlerClosure))))
    return JS_EXCEPTION;

  *h = (IOHandlerClosure){ctx, wsi, opaque_from_wsi(wsi, ctx)};

  return js_function_cclosure(ctx, lws_iohandler, 1, 0, h, lws_iohandler_free);
}

void
minnet_io_handlers(JSContext* ctx, struct lws* wsi, struct lws_pollargs args, JSValue out[2]) {
  struct wsi_opaque_user_data* opaque = opaque_from_wsi(wsi, ctx);

  if(JS_IsNull(opaque->handlers[0])) {
    JSValue func = minnet_io_handler(ctx, wsi);

    opaque->handlers[READ_HANDLER] = js_function_bind_1(ctx, func, JS_NewBool(ctx, READ_HANDLER));
    opaque->handlers[WRITE_HANDLER] = js_function_bind_1(ctx, func, JS_NewBool(ctx, WRITE_HANDLER));

    JS_FreeValue(ctx, func);
  }

  opaque->poll = (struct pollfd){args.fd, args.events, 0};

  out[0] = (args.events & POLLIN) ? JS_DupValue(ctx, opaque->handlers[READ_HANDLER]) : JS_NULL;
  out[1] = (args.events & POLLOUT) ? JS_DupValue(ctx, opaque->handlers[WRITE_HANDLER]) : JS_NULL;
}

static int
minnet_pollfds_handle(struct lws* wsi, struct js_callback* cb, struct lws_pollargs args) {
  JSValue argv[3] = {
      JS_NewInt32(cb->ctx, get_osf_handle(args.fd)),
      JS_NULL,
      JS_NULL,
  };

  minnet_io_handlers(cb->ctx, wsi, args, &argv[1]);
  callback_emit(cb, countof(argv), argv);

  js_argv_free(cb->ctx, countof(argv), argv);
  return 0;
}

int
minnet_pollfds_change(struct lws* wsi, enum lws_callback_reasons reason, struct js_callback* cb, struct lws_pollargs* args) {
  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: {
      break;
    }

    case LWS_CALLBACK_ADD_POLL_FD:
    case LWS_CALLBACK_DEL_POLL_FD:
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      if(callback_valid(cb))
        minnet_pollfds_handle(wsi, cb, *args);

      break;
    }

    default: {
      return -1;
    }
  }

  return 0;
}

struct FDCallbackClosure {
  JSContext* ctx;
  JSValue set_read, set_write;
  JSCFunctionMagic* set_handler;
};

static void
minnet_fd_callback_free(void* opaque) {
  struct FDCallbackClosure* closure = opaque;

  JS_FreeValue(closure->ctx, closure->set_read);
  JS_FreeValue(closure->ctx, closure->set_write);
  js_free(closure->ctx, closure);
}

static JSValue
minnet_fd_callback_closure(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  struct FDCallbackClosure* closure = opaque;
  JSValueConst args[] = {argv[0], JS_NULL};

  args[1] = argv[1];
  JS_Call(ctx, closure->set_read, JS_UNDEFINED, 2, args);

  args[1] = argv[2];
  JS_Call(ctx, closure->set_write, JS_UNDEFINED, 2, args);

  return JS_UNDEFINED;
}

/**
 * @brief      Returns a JS function which sets the read/write I/O callbacks (quickjs 'os' module) for an fd.
 *
 * @param      ctx   JS context
 *
 * @return     The function
 */
JSValue
minnet_default_fd_callback(JSContext* ctx) {
  JSValue os = js_global_get(ctx, "os");

  if(JS_IsObject(os)) {
    struct FDCallbackClosure* closure;

    if(!(closure = js_malloc(ctx, sizeof(struct FDCallbackClosure))))
      return JS_EXCEPTION;

    closure->ctx = ctx;
    closure->set_read = JS_GetPropertyStr(ctx, os, "setReadHandler");
    closure->set_write = JS_GetPropertyStr(ctx, os, "setWriteHandler");
    closure->set_handler = *((void**)JS_VALUE_GET_OBJ(closure->set_read) + 7);

    return js_function_cclosure(ctx, minnet_fd_callback_closure, 3, 0, closure, minnet_fd_callback_free);
  }

  return JS_ThrowTypeError(ctx, "globalThis.os must be imported module");
}

static void
minnet_log_callback(int level, const char* line) {
  if(minnet_log_ctx) {
    size_t n = 0, len = strlen(line);
    const char* x = line;

    if(JS_IsFunction(minnet_log_ctx, minnet_log_cb)) {
      n = skip_brackets(x, len);
      x += n;
      len -= n;
      n = skip_directory(x, len);
      x += n;
      len -= n;

      strip_trailing_newline(x, &len);

      JSValue argv[2] = {
          JS_NewInt32(minnet_log_ctx, level),
          JS_NewStringLen(minnet_log_ctx, x, len),
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
minnet_get_sessions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct list_head* el;
  JSValue ret;
  uint32_t i = 0;

  ret = JS_NewArray(ctx);

  list_for_each_prev(el, &opaque_list) {
    struct wsi_opaque_user_data* opaque = list_entry(el, struct wsi_opaque_user_data, link);
#ifdef DEBUG_OUTPUT
    lwsl_user("DEBUG                    %-22s @%u #%" PRId64 " %p", __func__, i, opaque->serial, opaque);
#endif

    JS_SetPropertyUint32(ctx, ret, i++, opaque->sess ? session_object(opaque->sess, ctx) : JS_NewInt64(ctx, opaque->serial));
  }

  return ret;
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
    JS_CFUNC_DEF("createServer", 1, minnet_server),
    JS_CFUNC_DEF("client", 1, minnet_client),
    JS_CFUNC_DEF("fetch", 1, minnet_fetch),
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

  // minnet_js_module = JS_ReadObject(ctx, qjsc_minnet, qjsc_minnet_size, JS_READ_OBJ_BYTECODE);

  JS_SetModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));

  minnet_response_init(ctx, m);
  minnet_request_init(ctx, m);
  minnet_ringbuffer_init(ctx, m);
  minnet_generator_init(ctx, m);
  minnet_ws_init(ctx, m);
  minnet_formparser_init(ctx, m);
  minnet_hash_init(ctx, m);
  minnet_asynciterator_init(ctx, m);
  minnet_url_init(ctx, m);
  minnet_headers_init(ctx, m);
  minnet_client_init(ctx, m);
  minnet_server_init(ctx, m);

  return 0;
}

UNUSED VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if(!(m = JS_NewCModule(ctx, module_name, js_minnet_init)))
    return NULL;

  JS_AddModuleExport(ctx, m, "Response");
  JS_AddModuleExport(ctx, m, "Request");
  JS_AddModuleExport(ctx, m, "Ringbuffer");
  JS_AddModuleExport(ctx, m, "Generator");
  JS_AddModuleExport(ctx, m, "Socket");
  JS_AddModuleExport(ctx, m, "FormParser");
  JS_AddModuleExport(ctx, m, "Hash");
  JS_AddModuleExport(ctx, m, "AsyncIterator");
  JS_AddModuleExport(ctx, m, "URL");
  JS_AddModuleExport(ctx, m, "Headers");
  JS_AddModuleExport(ctx, m, "Client");
  JS_AddModuleExport(ctx, m, "Server");

  JS_AddModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));

  minnet_log_ctx = ctx;

  lws_set_log_level(minnet_log_level, minnet_log_callback);

  return m;
}

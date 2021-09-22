#include "minnet-server.h"
#include "minnet-client.h"
#include "minnet-response.h"
#include "minnet-request.h"
#include "minnet-websocket.h"
#include "minnet-stream.h"
#include "jsutils.h"
#include "buffer.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <curl/curl.h>
#include <ctype.h>
#include <sys/time.h>

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

static JSValue minnet_log_cb, minnet_log_this;
int32_t minnet_log_level = 0;
JSContext* minnet_log_ctx = 0;
BOOL minnet_exception = FALSE;

static void
lws_log_callback(int level, const char* line) {
  if(minnet_log_ctx) {
    if(JS_IsUndefined(minnet_log_cb))
      js_console_log(minnet_log_ctx, &minnet_log_this, &minnet_log_cb);

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
    }
  }
}

int
minnet_lws_unhandled(const char* handler, int reason) {
  printf("Unhandled %s client event: %i %s\n", handler, reason, lws_callback_name(reason));
  return -1;
}

/*static JSValue
get_log(JSContext* ctx, JSValueConst this_val) {
  return JS_DupValue(ctx, minnet_log_cb);
}*/

static JSValue
set_log(JSContext* ctx, JSValueConst this_val, JSValueConst value, JSValueConst thisObj) {
  JSValue ret = minnet_log_cb;

  minnet_log_ctx = ctx;
  minnet_log_cb = JS_DupValue(ctx, value);
  if(!JS_IsUndefined(minnet_log_this) || !JS_IsNull(minnet_log_this))
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
  lws_set_log_level(minnet_log_level, lws_log_callback);
  return ret;
}

static JSValue
minnet_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  CURL* curl;
  CURLcode curlRes;
  const char* url;
  FILE* fi;
  MinnetResponse* res;
  uint8_t* buffer;
  long bufSize;
  long status;
  char* type;
  const char* body_str = NULL;
  struct curl_slist* headerlist = NULL;
  char* buf = calloc(1, 1);
  size_t bufsize = 1;

  JSValue resObj = JS_NewObjectClass(ctx, minnet_response_class_id);
  if(JS_IsException(resObj))
    return JS_EXCEPTION;

  res = js_mallocz(ctx, sizeof(*res));

  if(!res) {
    JS_FreeValue(ctx, resObj);
    return JS_EXCEPTION;
  }

  if(!JS_IsString(argv[0]))
    return JS_EXCEPTION;

  url = JS_ToCString(ctx, argv[0]);
  res->url = js_strdup(ctx, url);

  if(argc > 1 && JS_IsObject(argv[1])) {
    JSValue method, body, headers;
    const char* method_str;
    method = JS_GetPropertyStr(ctx, argv[1], "method");
    body = JS_GetPropertyStr(ctx, argv[1], "body");
    headers = JS_GetPropertyStr(ctx, argv[1], "headers");

    if(!JS_IsUndefined(headers)) {
      JSValue global_obj, object_ctor, /* object_proto, */ keys, names, length;
      int i;
      int32_t len;

      global_obj = JS_GetGlobalObject(ctx);
      object_ctor = JS_GetPropertyStr(ctx, global_obj, "Object");
      keys = JS_GetPropertyStr(ctx, object_ctor, "keys");

      names = JS_Call(ctx, keys, object_ctor, 1, (JSValueConst*)&headers);
      length = JS_GetPropertyStr(ctx, names, "length");

      JS_ToInt32(ctx, &len, length);

      for(i = 0; i < len; i++) {
        char* h;
        JSValue key, value;
        const char *key_str, *value_str;
        size_t key_len, value_len;
        key = JS_GetPropertyUint32(ctx, names, i);
        key_str = JS_ToCString(ctx, key);
        key_len = strlen(key_str);

        value = JS_GetPropertyStr(ctx, headers, key_str);
        value_str = JS_ToCString(ctx, value);
        value_len = strlen(value_str);

        buf = realloc(buf, bufsize + key_len + 2 + value_len + 2 + 1);
        h = &buf[bufsize];

        strcpy(&buf[bufsize], key_str);
        bufsize += key_len;
        strcpy(&buf[bufsize], ": ");
        bufsize += 2;
        strcpy(&buf[bufsize], value_str);
        bufsize += value_len;
        strcpy(&buf[bufsize], "\0\n");
        bufsize += 2;

        JS_FreeCString(ctx, key_str);
        JS_FreeCString(ctx, value_str);

        headerlist = curl_slist_append(headerlist, h);
      }

      JS_FreeValue(ctx, global_obj);
      JS_FreeValue(ctx, object_ctor);
      // JS_FreeValue(ctx, object_proto);
      JS_FreeValue(ctx, keys);
      JS_FreeValue(ctx, names);
      JS_FreeValue(ctx, length);
    }

    method_str = JS_ToCString(ctx, method);

    if(!JS_IsUndefined(body) || !strcasecmp(method_str, "post")) {
      body_str = JS_ToCString(ctx, body);
    }

    JS_FreeCString(ctx, method_str);

    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, body);
    JS_FreeValue(ctx, headers);
  }

  curl = curl_easy_init();
  if(!curl)
    return JS_EXCEPTION;

  fi = tmpfile();

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "minimal-network-quickjs");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fi);

  if(body_str)
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str);

  curlRes = curl_easy_perform(curl);
  if(curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK)
    res->status = status;

  if(curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type) == CURLE_OK)
    res->type = type ? js_strdup(ctx, type) : 0;

  res->ok = FALSE;

  if(curlRes != CURLE_OK) {
    fprintf(stderr, "CURL failed: %s\n", curl_easy_strerror(curlRes));
    goto finish;
  }

  bufSize = ftell(fi);
  rewind(fi);

  buffer = calloc(1, bufSize + 1);
  if(!buffer) {
    fclose(fi), fputs("memory alloc fails", stderr);
    goto finish;
  }

  /* copy the file into the buffer */
  if(1 != fread(buffer, bufSize, 1, fi)) {
    fclose(fi), free(buffer), fputs("entire read fails", stderr);
    goto finish;
  }

  fclose(fi);

  res->ok = TRUE;
  res->body = BUFFER_N(buffer, bufSize);

finish:
  curl_slist_free_all(headerlist);
  free(buf);
  if(body_str)
    JS_FreeCString(ctx, body_str);

  curl_easy_cleanup(curl);
  JS_SetOpaque(resObj, res);

  return resObj;
}

static const char*
io_events(int events) {
  switch(events & (POLLIN | POLLOUT)) {
    case POLLIN | POLLOUT: return "IN|OUT";
    case POLLIN: return "IN";
    case POLLOUT: return "OUT";
    case 0: return "0";
  }
  return "";
}
static int
io_parse_events(const char* str) {
  int events = 0;

  if(strstr(str, "IN"))
    events |= POLLIN;
  if(strstr(str, "OUT"))
    events |= POLLOUT;
  return events;
}

#define PIO (POLLIN | POLLOUT)

static JSValue
io_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
  struct pollfd x = {0, 0, 0};
  struct lws_context* context = value2ptr(ctx, func_data[2]);
  uint32_t fd, events, revents;
  int32_t wr;
  // static int calls = 0;

  const char* io = JS_ToCString(ctx, func_data[1]);
  revents = io_parse_events(io);

  JS_ToUint32(ctx, &fd, func_data[0]);
  JS_ToInt32(ctx, &wr, argv[0]);

  events = wr == WRITE_HANDLER ? POLLOUT : POLLIN;

  if(!(revents & PIO)) {
    x.fd = fd;
    x.events = events;
    x.revents = 0;

    if(poll(&x, 1, 0) < 0) {
      printf("poll error: %s\n", strerror(errno));
    } else {
      revents = x.revents;
    }
  }

  //  if(++calls <= 100)
  // printf("io_handler #%i fd = %d, wr = %s, events = %s, revents = %s, context = %p\n", ++calls, fd, wr ? "W" : "R", io_parse_events(io), io_events(revents), context);

  if(revents & PIO) {
    x.fd = fd;
    x.events = events;
    x.revents = revents;

    lws_service_fd(context, &x);
  }
  /*  if(++calls <= 100)
      printf("minnet %s handler calls=%i fd=%d events=%d revents=%d pfd=[%d "
             "%d %d]\n",
             wr == WRITE_HANDLER ? "writable" : "readable",
             calls,
             fd,
             events,
             revents,
             x.fd,
             x.events,
             x.revents);*/

  return JS_UNDEFINED;
}

// static uint32_t handler_seq = 0;

static JSValue
make_handler(JSContext* ctx, int fd, int events, struct lws* wsi, int magic) {
  // uint32_t seq = ++handler_seq;
  struct lws_context* context = lws_get_context(wsi);
  // intptr_t values[] = {fd, events, 0, (intptr_t)context, seq};
  // JSValue buffer = JS_NewArrayBufferCopy(ctx, (const void*)values, sizeof(values));

  // printf("make_handler fd = %d, events = %s, context = %p\n", fd, io_events(events), context);

  JSValue data[3] = {JS_NewUint32(ctx, fd), JS_NewString(ctx, io_events(events)), ptr2value(ctx, context)};
  return JS_NewCFunctionData(ctx, io_handler, 0, magic, countof(data), data);
}

void
minnet_handlers(JSContext* ctx, struct lws* wsi, struct lws_pollargs* args, JSValue out[2]) {
  JSValue func = make_handler(ctx, args->fd, args->events | args->prev_events, wsi, 0);

  out[0] = (args->events & POLLIN) ? js_function_bind_1(ctx, JS_DupValue(ctx, func), JS_NewInt32(ctx, READ_HANDLER)) : JS_NULL;
  out[1] = (args->events & POLLOUT) ? js_function_bind_1(ctx, JS_DupValue(ctx, func), JS_NewInt32(ctx, WRITE_HANDLER)) : JS_NULL;

  JS_FreeValue(ctx, func);
}

JSValue
minnet_emit_this(const struct ws_callback* cb, JSValueConst this_obj, int argc, JSValue* argv) {
  if(!cb->ctx)
    return JS_UNDEFINED;

  size_t len;
  const char* str = JS_ToCStringLen(cb->ctx, &len, cb->func_obj);
  // printf("emit %s [%d] \"%.*s\"\n", cb->name, argc, (int)((const char*)memchr(str, '{', len) - str), str);
  JS_FreeCString(cb->ctx, str);

  return JS_Call(cb->ctx, cb->func_obj, this_obj, argc, argv);
}

JSValue
minnet_emit(const struct ws_callback* cb, int argc, JSValue* argv) {
  return minnet_emit_this(cb, cb->this_obj /* ? *cb->this_obj : JS_NULL*/, argc, argv);
}

static const JSCFunctionListEntry minnet_funcs[] = {
    JS_CFUNC_DEF("server", 1, minnet_ws_server),
    JS_CFUNC_DEF("client", 1, minnet_ws_client),
    JS_CFUNC_DEF("fetch", 1, minnet_fetch),
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
  printf("\n\t%s\t%s", n, str);
  fflush(stdout);
  JS_FreeCString(ctx, str);
}

static int
js_minnet_init(JSContext* ctx, JSModuleDef* m) {
  minnet_log_cb = JS_UNDEFINED;
  minnet_log_this = JS_UNDEFINED;

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

  // Add class WebSocket
  JS_NewClassID(&minnet_ws_class_id);
  JS_NewClass(JS_GetRuntime(ctx), minnet_ws_class_id, &minnet_ws_class);
  minnet_ws_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_ws_proto, minnet_ws_proto_funcs, minnet_ws_proto_funcs_size);
  JS_SetPropertyFunctionList(ctx, minnet_ws_proto, minnet_ws_proto_defs, minnet_ws_proto_defs_size);

  minnet_ws_ctor = JS_NewCFunction2(ctx, minnet_ws_constructor, "MinnetWebsocket", 0, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, minnet_ws_ctor, minnet_ws_proto);

  JS_SetPropertyFunctionList(ctx, minnet_ws_ctor, minnet_ws_proto_defs, minnet_ws_proto_defs_size);

  if(m)
    JS_SetModuleExport(ctx, m, "Socket", minnet_ws_ctor);

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

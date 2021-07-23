#include "minnet.h"
#include "server.h"
#include "client.h"
#include "list.h"
#include <assert.h>
#include <curl/curl.h>
#include <sys/time.h>

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

JSValue minnet_log, minnet_log_this;
JSContext* minnet_log_ctx = 0;
BOOL minnet_exception = FALSE;

static void
lws_log_callback(int level, const char* line) {
  if(minnet_log_ctx) {
    if(JS_VALUE_GET_TAG(minnet_log) == 0 && JS_VALUE_GET_TAG(minnet_log_this) == 0)
      get_console_log(minnet_log_ctx, &minnet_log_this, &minnet_log);

    if(JS_IsFunction(minnet_log_ctx, minnet_log)) {
      size_t len = strlen(line);
      JSValueConst argv[2] = {JS_NewString(minnet_log_ctx, "minnet"),
                              JS_NewStringLen(minnet_log_ctx, line, len > 0 && line[len - 1] == '\n' ? len - 1 : len)};
      JSValue ret = JS_Call(minnet_log_ctx, minnet_log, minnet_log_this, 2, argv);

      if(JS_IsException(ret))
        minnet_exception = TRUE;

      JS_FreeValue(minnet_log_ctx, argv[0]);
      JS_FreeValue(minnet_log_ctx, argv[1]);
      JS_FreeValue(minnet_log_ctx, ret);
    }
  }
}

static JSValue
minnet_service_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
  int32_t rw = 0;
  uint32_t calls = ++func_data[3].u.int32;
  struct lws_pollfd pfd;
  struct lws_pollargs args = *(struct lws_pollargs*)&JS_VALUE_GET_PTR(func_data[4]);
  struct lws_context* context = JS_VALUE_GET_PTR(func_data[2]);

  if(argc >= 1)
    JS_ToInt32(ctx, &rw, argv[0]);

  pfd.fd = JS_VALUE_GET_INT(func_data[0]);
  pfd.revents = rw ? POLLOUT : POLLIN;
  pfd.events = JS_VALUE_GET_INT(func_data[1]);

  if(pfd.events != (POLLIN | POLLOUT) || poll(&pfd, 1, 0) > 0)
    lws_service_fd(context, &pfd);

  /*if (calls <= 100)
    printf("minnet %s handler calls=%i fd=%d events=%d revents=%d pfd=[%d "
         "%d %d]\n",
         rw ? "writable" : "readable", calls, pfd.fd, pfd.events,
         pfd.revents, args.fd, args.events, args.prev_events);*/

  return JS_UNDEFINED;
}

void
minnet_ws_sslcert(JSContext* ctx, struct lws_context_creation_info* info, JSValueConst options) {
  JSValue opt_ssl_cert = JS_GetPropertyStr(ctx, options, "sslCert");
  JSValue opt_ssl_private_key = JS_GetPropertyStr(ctx, options, "sslPrivateKey");
  JSValue opt_ssl_ca = JS_GetPropertyStr(ctx, options, "sslCA");

  if(JS_IsString(opt_ssl_cert))
    info->ssl_cert_filepath = JS_ToCString(ctx, opt_ssl_cert);
  if(JS_IsString(opt_ssl_private_key))
    info->ssl_private_key_filepath = JS_ToCString(ctx, opt_ssl_private_key);
  if(JS_IsString(opt_ssl_ca))
    info->client_ssl_ca_filepath = JS_ToCString(ctx, opt_ssl_ca);
}

static JSValue
minnet_ws_send(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetWebsocket* ws_obj;
  const char* msg;
  uint8_t* data;
  size_t len;
  int m, n;

  if(!(ws_obj = JS_GetOpaque2(ctx, this_val, minnet_ws_class_id)))
    return JS_EXCEPTION;

  if(JS_IsString(argv[0])) {
    msg = JS_ToCString(ctx, argv[0]);
    len = strlen(msg);
    uint8_t buffer[LWS_PRE + len];

    n = lws_snprintf((char*)&buffer[LWS_PRE], len + 1, "%s", msg);
    m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_TEXT);
    if(m < n) {
      // Sending message failed
      return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
  }

  data = JS_GetArrayBuffer(ctx, &len, argv[0]);
  if(data) {
    uint8_t buffer[LWS_PRE + len];
    memcpy(&buffer[LWS_PRE], data, len);

    m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_BINARY);
    if(m < len) {
      // Sending data failed
      return JS_EXCEPTION;
    }
  }
  return JS_UNDEFINED;
}

static JSValue
minnet_ws_respond(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  MinnetWebsocket* ws_obj;
  JSValue ret = JS_UNDEFINED;
  struct http_header* header;

  if(!(ws_obj = JS_GetOpaque2(ctx, this_val, minnet_ws_class_id)))
    return JS_EXCEPTION;

  if((header = ws_obj->header) == 0) {
    header = ws_obj->header = js_mallocz(ctx, sizeof(struct http_header));
  }

  switch(magic) {
    case 0: {
      const char* msg = 0;
      uint32_t status = 0;

      JS_ToUint32(ctx, &status, argv[0]);

      if(argc >= 2)
        msg = JS_ToCString(ctx, argv[1]);

      lws_return_http_status(ws_obj->lwsi, status, msg);
      if(msg)
        JS_FreeCString(ctx, msg);
      break;
    }
    case 1: {

      const char* msg = 0;
      size_t len = 0;
      uint32_t status = 0;

      JS_ToUint32(ctx, &status, argv[0]);

      if(argc >= 2)
        msg = JS_ToCStringLen(ctx, &len, argv[1]);

      if(lws_http_redirect(ws_obj->lwsi, status, (unsigned char*)msg, len, &header->pos, header->end) < 0)
        ret = JS_NewInt32(ctx, -1);
      if(msg)
        JS_FreeCString(ctx, msg);
      break;
    }
    case 2: {
      size_t namelen;
      const char* namestr = JS_ToCStringLen(ctx, &namelen, argv[0]);
      char* name = js_malloc(ctx, namelen + 2);
      size_t len;
      const char* value = JS_ToCStringLen(ctx, &len, argv[1]);

      memcpy(name, namestr, namelen);
      name[namelen] = ':';
      name[namelen + 1] = '\0';

      if(lws_add_http_header_by_name(ws_obj->lwsi, name, value, len, &header->pos, header->end) < 0)
        ret = JS_NewInt32(ctx, -1);

      js_free(ctx, name);
      JS_FreeCString(ctx, namestr);
      JS_FreeCString(ctx, value);
      break;
    }
  }

  return ret;
}

static JSValue
minnet_ws_ping(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetWebsocket* ws_obj;
  uint8_t* data;
  size_t len;

  if(!(ws_obj = JS_GetOpaque2(ctx, this_val, minnet_ws_class_id)))
    return JS_EXCEPTION;

  data = JS_GetArrayBuffer(ctx, &len, argv[0]);
  if(data) {
    uint8_t buffer[len + LWS_PRE];
    memcpy(&buffer[LWS_PRE], data, len);

    int m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_PING);
    if(m < len) {
      // Sending ping failed
      return JS_EXCEPTION;
    }
  } else {
    uint8_t buffer[LWS_PRE];
    lws_write(ws_obj->lwsi, &buffer[LWS_PRE], 0, LWS_WRITE_PING);
  }
  return JS_UNDEFINED;
}

static JSValue
minnet_ws_pong(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetWebsocket* ws_obj;
  uint8_t* data;
  size_t len;

  if(!(ws_obj = JS_GetOpaque2(ctx, this_val, minnet_ws_class_id)))
    return JS_EXCEPTION;

  data = JS_GetArrayBuffer(ctx, &len, argv[0]);
  if(data) {
    uint8_t buffer[len + LWS_PRE];
    memcpy(&buffer[LWS_PRE], data, len);

    int m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_PONG);
    if(m < len) {
      // Sending pong failed
      return JS_EXCEPTION;
    }
  } else {
    uint8_t buffer[LWS_PRE];
    lws_write(ws_obj->lwsi, &buffer[LWS_PRE], 0, LWS_WRITE_PONG);
  }
  return JS_UNDEFINED;
}

static JSValue
minnet_ws_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetWebsocket* ws_obj;
  const char* reason = 0;
  size_t rlen = 0;

  if(!(ws_obj = JS_GetOpaque2(ctx, this_val, minnet_ws_class_id)))
    return JS_EXCEPTION;

  if(ws_obj->lwsi) {
    int optind = 0;
    uint32_t status = LWS_CLOSE_STATUS_NORMAL;

    if(optind < argc && JS_IsNumber(argv[optind]))
      JS_ToInt32(ctx, &status, argv[optind++]);

    if(optind < argc) {
      reason = JS_ToCStringLen(ctx, &rlen, argv[optind++]);
      if(rlen > 124)
        rlen = 124;
    }

    if(reason)
      lws_close_reason(ws_obj->lwsi, status, reason, rlen);

    lws_close_free_wsi(ws_obj->lwsi, status, "minnet_ws_close");

    ws_obj->lwsi = 0;
    return JS_TRUE;
  }

  return JS_FALSE;
}

static JSValue
minnet_ws_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetWebsocket* ws_obj;
  JSValue ret = JS_UNDEFINED;
  if(!(ws_obj = JS_GetOpaque2(ctx, this_val, minnet_ws_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case 0: {
      ret = JS_NewInt32(ctx, lws_get_socket_fd(ws_obj->lwsi));
      break;
    }
    case 1: {
      char address[1024];
      lws_get_peer_simple(ws_obj->lwsi, address, sizeof(address));

      ret = JS_NewString(ctx, address);
      break;
    }
    case 2:
    case 3: {
      struct sockaddr_in addr;
      socklen_t addrlen = sizeof(addr);
      int fd = lws_get_socket_fd(ws_obj->lwsi);

      if(getpeername(fd, &addr, &addrlen) != -1) {
        ret = JS_NewInt32(ctx, magic == 2 ? addr.sin_family : addr.sin_port);
      }
      break;
    }
    case 4: {
      struct sockaddr_in addr;
      socklen_t addrlen = sizeof(addr);
      int fd = lws_get_socket_fd(ws_obj->lwsi);

      if(getpeername(fd, &addr, &addrlen) != -1) {
        ret = JS_NewArrayBufferCopy(ctx, &addr, addrlen);
      }
      break;
    }
  }
  return ret;
}

static void
minnet_ws_finalizer(JSRuntime* rt, JSValue val) {
  MinnetWebsocket* ws_obj = JS_GetOpaque(val, minnet_ws_class_id);
  if(ws_obj) {
    if(--ws_obj->ref_count == 0)
      js_free_rt(rt, ws_obj);
  }
}

#include "server.h"
#include "client.h"

static const JSCFunctionListEntry minnet_funcs[] = {
    JS_CFUNC_DEF("server", 1, minnet_ws_server),
    JS_CFUNC_DEF("client", 1, minnet_ws_client),
    JS_CFUNC_DEF("fetch", 1, minnet_fetch),
    JS_CFUNC_DEF("setLog", 1, minnet_set_log),
};

static int
js_minnet_init(JSContext* ctx, JSModuleDef* m) {
  return JS_SetModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));
}

__attribute__((visibility("default"))) JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, js_minnet_init);
  if(!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));

  // Add class Response
  JS_NewClassID(&minnet_response_class_id);
  JS_NewClass(JS_GetRuntime(ctx), minnet_response_class_id, &minnet_response_class);
  JSValue response_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, response_proto, minnet_response_proto_funcs, countof(minnet_response_proto_funcs));
  JS_SetClassProto(ctx, minnet_response_class_id, response_proto);

  // Add class WebSocket
  JS_NewClassID(&minnet_ws_class_id);
  JS_NewClass(JS_GetRuntime(ctx), minnet_ws_class_id, &minnet_ws_class);
  JSValue websocket_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, websocket_proto, minnet_ws_proto_funcs, countof(minnet_ws_proto_funcs));
  JS_SetClassProto(ctx, minnet_ws_class_id, websocket_proto);

  minnet_log_ctx = ctx;

  lws_set_log_level(LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE, lws_log_callback);

  return m;
}

JSValue
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

  res->url = argv[0];
  url = JS_ToCString(ctx, argv[0]);

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
    res->status = JS_NewInt32(ctx, (int32_t)status);

  if(curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type) == CURLE_OK)
    res->type = type ? JS_NewString(ctx, type) : JS_NULL;

  res->ok = JS_FALSE;

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

  res->ok = JS_TRUE;
  res->buffer = buffer;
  res->size = bufSize;

finish:
  curl_slist_free_all(headerlist);
  free(buf);
  if(body_str)
    JS_FreeCString(ctx, body_str);

  curl_easy_cleanup(curl);
  JS_SetOpaque(resObj, res);

  return resObj;
}

static JSValue
minnet_response_buffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res && res->buffer) {
    JSValue val = JS_NewArrayBufferCopy(ctx, res->buffer, res->size);
    return val;
  }

  return JS_EXCEPTION;
}

static JSValue
minnet_response_json(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res && res->buffer)
    return JS_ParseJSON(ctx, (char*)res->buffer, res->size, "<input>");

  return JS_EXCEPTION;
}

static JSValue
minnet_response_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res && res->buffer)
    return JS_NewStringLen(ctx, (char*)res->buffer, res->size);

  return JS_EXCEPTION;
}

static JSValue
minnet_response_getter_ok(JSContext* ctx, JSValueConst this_val) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return res->ok;

  return JS_EXCEPTION;
}

static JSValue
minnet_response_getter_url(JSContext* ctx, JSValueConst this_val) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return res->url;

  return JS_EXCEPTION;
}

static JSValue
minnet_response_getter_status(JSContext* ctx, JSValueConst this_val) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res)
    return res->status;

  return JS_EXCEPTION;
}

static JSValue
minnet_response_getter_type(JSContext* ctx, JSValueConst this_val) {
  MinnetResponse* res = JS_GetOpaque(this_val, minnet_response_class_id);
  if(res) {
    return res->type;
  }

  return JS_EXCEPTION;
}

static void
minnet_response_finalizer(JSRuntime* rt, JSValue val) {
  MinnetResponse* res = JS_GetOpaque(val, minnet_response_class_id);
  if(res) {
    if(res->buffer)
      free(res->buffer);
    js_free_rt(rt, res);
  }
}

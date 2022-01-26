#include "minnet-buffer.h"
#include "jsutils.h"
#include "minnet-websocket.h"
#include "minnet-server.h"
#include "minnet-server-http.h"
#include <strings.h>

int64_t ws_serial = 0;

THREAD_LOCAL JSValue minnet_ws_proto, minnet_ws_ctor;
THREAD_LOCAL JSClassID minnet_ws_class_id;

enum { WEBSOCKET_FD, WEBSOCKET_ADDRESS, WEBSOCKET_FAMILY, WEBSOCKET_PORT, WEBSOCKET_PEER, WEBSOCKET_SSL, WEBSOCKET_BINARY, WEBSOCKET_READYSTATE };
enum { RESPONSE_BODY, RESPONSE_HEADER, RESPONSE_REDIRECT };

static JSValue
minnet_ws_new(JSContext* ctx, struct lws* wsi) {
  MinnetWebsocket* ws;
  struct wsi_opaque_user_data* opaque;
  JSValue ws_obj = JS_NewObjectProtoClass(ctx, minnet_ws_proto, minnet_ws_class_id);

  if(JS_IsException(ws_obj))
    return JS_EXCEPTION;

  if(!(ws = js_mallocz(ctx, sizeof(MinnetWebsocket)))) {
    JS_FreeValue(ctx, ws_obj);
    return JS_EXCEPTION;
  }

  ws->lwsi = wsi;
  ws->ref_count = 1;

  JS_SetOpaque(ws_obj, ws);

  if((opaque = lws_opaque(wsi, ctx))) {
    opaque->obj = JS_VALUE_GET_OBJ(JS_DupValue(ctx, ws_obj));
    opaque->ws = ws;
    opaque->status = 0;
    opaque->handler = JS_NULL;
    /*opaque->handlers[0] = JS_NULL;
    opaque->handlers[1] = JS_NULL;*/
  }

  return ws_obj;
}

/*MinnetWebsocket*
ws_from_wsi(struct lws* wsi) {
  struct wsi_opaque_user_data* opaque;

  if((opaque = lws_get_opaque_user_data(wsi)))
    return opaque->ws ? opaque->ws : minnet_ws_data(JS_MKPTR(JS_TAG_OBJECT, opaque->obj));

  return 0;
}*/

MinnetWebsocket*
ws_from_wsi2(struct lws* wsi, JSContext* ctx) {
  JSValue ws_obj = minnet_ws_object(ctx, wsi);
  return minnet_ws_data(ws_obj);
}

JSValue
minnet_ws_object(JSContext* ctx, struct lws* wsi) {
  struct wsi_opaque_user_data* opaque;

  if((opaque = lws_get_opaque_user_data(wsi))) {
    JSValue ws_obj;
    if(opaque->obj && opaque->ws) {
      ws_obj = JS_DupValue(ctx, JS_MKPTR(JS_TAG_OBJECT, opaque->obj));
      /*      if(!(opaque->ws = minnet_ws_data2(ctx, ws_obj)))
              return JS_EXCEPTION;*/
      opaque->ws->ref_count++;
    } else {
      ws_obj = minnet_ws_wrap(ctx, wsi);
      opaque->obj = JS_VALUE_GET_OBJ(ws_obj);
      opaque->ws = minnet_ws_data(ws_obj);
    }
    return ws_obj;
  }

  return minnet_ws_new(ctx, wsi);
}

JSValue
minnet_ws_wrap(JSContext* ctx, struct lws* wsi) {
  MinnetWebsocket* ws;
  struct wsi_opaque_user_data* opaque;
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_ws_proto, minnet_ws_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  ws = js_mallocz(ctx, sizeof(MinnetWebsocket));

  ws->lwsi = wsi;
  ws->ref_count = 1;

  JS_SetOpaque(ret, ws);

  if((opaque = lws_opaque(wsi, ctx))) {
    opaque->obj = JS_VALUE_GET_OBJ(ret);
    opaque->ws = ws;
    opaque->handler = JS_NULL;
    /*    opaque->handlers[0] = JS_NULL;
        opaque->handlers[1] = JS_NULL;*/
  }

  return ret;
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
    info->ssl_ca_filepath = JS_ToCString(ctx, opt_ssl_ca);
}

static JSValue
minnet_ws_send(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetWebsocket* ws;
  int64_t m, len;
  MinnetSession* sess;
  JSValue ret = JS_UNDEFINED;
  struct wsi_opaque_user_data* opaque;
  MinnetBuffer buffer = BUFFER(0);

  if(!(ws = minnet_ws_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!(m = buffer_fromvalue(&buffer, argv[0], ctx)))
    return JS_ThrowTypeError(ctx, "argument 1 expecting String/ArrayBuffer");

  if(m < 0) {
    ret = JS_ThrowOutOfMemory(ctx);
    goto fail;
  }

  len = buffer_BYTES(&buffer);

  if((sess = ws_session(ws))) {

    buffer_append(&sess->send_buf, buffer.read, len, ctx);
    lws_callback_on_writable(ws->lwsi);

  } else {

    m = lws_write(ws->lwsi, buffer.read, buffer_BYTES(&buffer), JS_IsString(argv[0]) ? LWS_WRITE_TEXT : LWS_WRITE_BINARY);

    if(m < len)
      ret = JS_ThrowInternalError(ctx, "lws write failed: %" PRIi64 "/%" PRIi64, m, len);
    else
      ret = JS_NewInt64(ctx, m);
  }

fail:
  buffer_free(&buffer, JS_GetRuntime(ctx));

  return ret;
}

static JSValue
minnet_ws_respond(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
  MinnetWebsocket* ws;
  JSValue ret = JS_UNDEFINED;

  if(!(ws = minnet_ws_data2(ctx, this_val)))
    return JS_EXCEPTION;

  MinnetBuffer header = {0, 0, 0, 0, 0};

  switch(magic) {
    case RESPONSE_BODY: {
      const char* msg = 0;
      uint32_t status = 0;

      JS_ToUint32(ctx, &status, argv[0]);
      if(argc >= 2)
        msg = JS_ToCString(ctx, argv[1]);
      lws_return_http_status(ws->lwsi, status, msg);
      if(msg)
        JS_FreeCString(ctx, msg);
      break;
    }
    case RESPONSE_REDIRECT: {
      const char* msg = 0;
      size_t len = 0;
      uint32_t status = 0;
      JS_ToUint32(ctx, &status, argv[0]);

      if(argc >= 2)
        msg = JS_ToCStringLen(ctx, &len, argv[1]);
      if(lws_http_redirect(ws->lwsi, status, (unsigned char*)msg, len, &header.write, header.end) < 0)
        ret = JS_NewInt32(ctx, -1);
      if(msg)
        JS_FreeCString(ctx, msg);
      break;
    }
    case RESPONSE_HEADER: {
      size_t namelen;
      const char* namestr = JS_ToCStringLen(ctx, &namelen, argv[0]);
      char* name = js_malloc(ctx, namelen + 2);
      size_t len;
      const char* value = JS_ToCStringLen(ctx, &len, argv[1]);
      memcpy(name, namestr, namelen);
      name[namelen] = ':';
      name[namelen + 1] = '\0';

      if(lws_add_http_header_by_name(ws->lwsi, (const uint8_t*)name, (const uint8_t*)value, len, &header.write, header.end) < 0)
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
  MinnetWebsocket* ws;
  uint8_t* data;
  size_t len;

  if(!(ws = minnet_ws_data2(ctx, this_val)))
    return JS_EXCEPTION;

  data = JS_GetArrayBuffer(ctx, &len, argv[0]);
  if(data) {
    uint8_t buffer[len + LWS_PRE];
    memcpy(&buffer[LWS_PRE], data, len);

    int m = lws_write(ws->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_PING);
    if((size_t)m < len) {
      // Sending ping failed
      return JS_EXCEPTION;
    }
  } else {
    uint8_t buffer[LWS_PRE];
    lws_write(ws->lwsi, &buffer[LWS_PRE], 0, LWS_WRITE_PING);
  }
  return JS_UNDEFINED;
}

static JSValue
minnet_ws_pong(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetWebsocket* ws;
  uint8_t* data;
  size_t len;

  if(!(ws = minnet_ws_data2(ctx, this_val)))
    return JS_EXCEPTION;

  data = JS_GetArrayBuffer(ctx, &len, argv[0]);
  if(data) {
    uint8_t buffer[len + LWS_PRE];
    memcpy(&buffer[LWS_PRE], data, len);

    int m = lws_write(ws->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_PONG);
    if((size_t)m < len) {
      // Sending pong failed
      return JS_EXCEPTION;
    }
  } else {
    uint8_t buffer[LWS_PRE];
    lws_write(ws->lwsi, &buffer[LWS_PRE], 0, LWS_WRITE_PONG);
  }
  return JS_UNDEFINED;
}

static JSValue
minnet_ws_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  MinnetWebsocket* ws;
  const char* reason = 0;
  size_t rlen = 0;

  if(!(ws = minnet_ws_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(ws->lwsi) {
    int optind = 0;
    int32_t status = LWS_CLOSE_STATUS_NORMAL;
    struct wsi_opaque_user_data* opaque;

    while(optind < argc) {
      if(JS_IsNumber(argv[optind]) || optind + 1 < argc) {
        JS_ToInt32(ctx, &status, argv[optind]);
      } else {
        reason = JS_ToCStringLen(ctx, &rlen, argv[optind]);
        if(rlen > 124)
          rlen = 124;
      }
      optind++;
    }

    opaque = ws_opaque(ws);
    assert(opaque);

    // printf("minnet_ws_close fd=%d reason=%s\n", lws_get_socket_fd(ws->lwsi), reason);
    if(opaque->status < CLOSING) {
      struct lws_protocols* protocol = lws_get_protocol(ws->lwsi);

      if(!strncmp(protocol->name, "ws", 2))
        lws_close_reason(ws->lwsi, status, (uint8_t*)reason, rlen);
    }

    opaque->status = CLOSED;

    lws_close_free_wsi(ws->lwsi, status, "minnet_ws_close");
    ws->lwsi = 0;

    return JS_TRUE;
  }

  return JS_FALSE;
}

static JSValue
minnet_ws_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetWebsocket* ws;
  JSValue ret = JS_UNDEFINED;

  if(!(ws = minnet_ws_data(this_val)))
    return JS_UNDEFINED;

  if(!ws->lwsi)
    return JS_NULL;

  switch(magic) {
    case WEBSOCKET_FD: {
      ret = JS_NewInt32(ctx, lws_get_socket_fd(lws_get_network_wsi(ws->lwsi)));
      break;
    }
    case WEBSOCKET_ADDRESS: {
      struct sockaddr_in addr;
      socklen_t addrlen = sizeof(addr);
      int fd = lws_get_socket_fd(lws_get_network_wsi(ws->lwsi));

      if(getpeername(fd, (struct sockaddr*)&addr, &addrlen) != -1) {
        char address[1024];
        lws_get_peer_simple(ws->lwsi, address, sizeof(address));

        ret = JS_NewString(ctx, address);
      }
      break;
    }
    case WEBSOCKET_FAMILY:
    case WEBSOCKET_PORT: {
      struct sockaddr_in addr;
      socklen_t addrlen = sizeof(addr);
      int fd = lws_get_socket_fd(lws_get_network_wsi(ws->lwsi));

      if(getpeername(fd, (struct sockaddr*)&addr, &addrlen) != -1) {
        ret = JS_NewInt32(ctx, magic == 2 ? addr.sin_family : ntohs(addr.sin_port));
      }
      break;
    }
    case WEBSOCKET_PEER: {
      struct sockaddr_in addr;
      socklen_t addrlen = sizeof(addr);
      int fd = lws_get_socket_fd(lws_get_network_wsi(ws->lwsi));

      if(getpeername(fd, (struct sockaddr*)&addr, &addrlen) != -1) {
        ret = JS_NewArrayBufferCopy(ctx, (const uint8_t*)&addr, addrlen);
      }
      break;
    }
    case WEBSOCKET_SSL: {
      ret = JS_NewBool(ctx, lws_is_ssl(lws_get_network_wsi(ws->lwsi)));
      break;
    }
    case WEBSOCKET_BINARY: {
      ret = JS_NewBool(ctx, ws->binary);
      break;
    }
    case WEBSOCKET_READYSTATE: {
      ret = JS_NewUint32(ctx, ws_opaque(ws)->status);
      break;
    }
  }
  return ret;
}

static JSValue
minnet_ws_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetWebsocket* ws;
  JSValue ret = JS_UNDEFINED;

  if(!(ws = minnet_ws_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {

    case WEBSOCKET_BINARY: {
      ws->binary = JS_ToBool(ctx, value);
      break;
    }
  }
  return ret;
}

JSValue
minnet_ws_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetWebsocket* ws;

  if(!(ws = js_mallocz(ctx, sizeof(MinnetWebsocket))))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_ws_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_ws_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  if(argc > 0) {
    if(JS_IsNumber(argv[0])) {
      uint32_t fd;
      JS_ToUint32(ctx, &fd, argv[0]);
      ws->lwsi = lws_adopt_socket(minnet_server.context.lws, fd);
    }
  }

  JS_SetOpaque(obj, ws);

  if(ws->lwsi) {
    struct wsi_opaque_user_data* opaque = js_malloc(ctx, sizeof(struct wsi_opaque_user_data));
    opaque->obj = JS_VALUE_GET_OBJ(JS_DupValue(ctx, obj));
    opaque->handler = JS_NULL;
    /*opaque->handlers[0] = JS_NULL;
    opaque->handlers[1] = JS_NULL;*/
    lws_set_opaque_user_data(ws->lwsi, opaque);
  }

  return obj;

fail:
  js_free(ctx, ws);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
minnet_ws_finalizer(JSRuntime* rt, JSValue val) {
  MinnetWebsocket* ws;
  if((ws = minnet_ws_data(val))) {
    if(--ws->ref_count == 0)
      js_free_rt(rt, ws);
  }
}

JSClassDef minnet_ws_class = {
    "MinnetWebSocket",
    .finalizer = minnet_ws_finalizer,
};

const JSCFunctionListEntry minnet_ws_proto_funcs[] = {
    JS_CFUNC_DEF("send", 1, minnet_ws_send),
    JS_CFUNC_MAGIC_DEF("respond", 1, minnet_ws_respond, RESPONSE_BODY),
    JS_CFUNC_MAGIC_DEF("redirect", 2, minnet_ws_respond, RESPONSE_REDIRECT),
    JS_CFUNC_MAGIC_DEF("header", 2, minnet_ws_respond, RESPONSE_HEADER),
    JS_CFUNC_DEF("ping", 1, minnet_ws_ping),
    JS_CFUNC_DEF("pong", 1, minnet_ws_pong),
    JS_CFUNC_DEF("close", 1, minnet_ws_close),
    JS_CGETSET_MAGIC_FLAGS_DEF("fd", minnet_ws_get, 0, WEBSOCKET_FD, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("address", minnet_ws_get, 0, WEBSOCKET_ADDRESS, 0),
    JS_ALIAS_DEF("remoteAddress", "address"),
    JS_CGETSET_MAGIC_FLAGS_DEF("family", minnet_ws_get, 0, WEBSOCKET_FAMILY, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("port", minnet_ws_get, 0, WEBSOCKET_PORT, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("peer", minnet_ws_get, 0, WEBSOCKET_PEER, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("ssl", minnet_ws_get, 0, WEBSOCKET_SSL, 0),
    JS_ALIAS_DEF("tls", "ssl"),
    JS_CGETSET_MAGIC_FLAGS_DEF("binary", minnet_ws_get, minnet_ws_set, WEBSOCKET_BINARY, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("readyState", minnet_ws_get, 0, WEBSOCKET_READYSTATE, JS_PROP_ENUMERABLE),
    JS_ALIAS_DEF("remote", "peer"),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetWebSocket", JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("CONNECTING", 0, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("OPEN", 1, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("CLOSING", 2, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("CLOSED", 3, JS_PROP_CONFIGURABLE),
};

const JSCFunctionListEntry minnet_ws_static_funcs[] = {
    JS_PROP_INT32_DEF("CONNECTING", 0, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("OPEN", 1, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("CLOSING", 2, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("CLOSED", 3, JS_PROP_ENUMERABLE),
};

const JSCFunctionListEntry minnet_ws_proto_defs[] = {
    JS_PROP_INT32_DEF("CLOSE_STATUS_NORMAL", LWS_CLOSE_STATUS_NORMAL, 0),
    JS_PROP_INT32_DEF("CLOSE_STATUS_GOINGAWAY", LWS_CLOSE_STATUS_GOINGAWAY, 0),
    JS_PROP_INT32_DEF("CLOSE_STATUS_PROTOCOL_ERR", LWS_CLOSE_STATUS_PROTOCOL_ERR, 0),
    JS_PROP_INT32_DEF("CLOSE_STATUS_UNACCEPTABLE_OPCODE", LWS_CLOSE_STATUS_UNACCEPTABLE_OPCODE, 0),
    JS_PROP_INT32_DEF("CLOSE_STATUS_RESERVED", LWS_CLOSE_STATUS_RESERVED, 0),
    JS_PROP_INT32_DEF("CLOSE_STATUS_NO_STATUS", LWS_CLOSE_STATUS_NO_STATUS, 0),
    JS_PROP_INT32_DEF("CLOSE_STATUS_ABNORMAL_CLOSE", LWS_CLOSE_STATUS_ABNORMAL_CLOSE, 0),
    JS_PROP_INT32_DEF("CLOSE_STATUS_INVALID_PAYLOAD", LWS_CLOSE_STATUS_INVALID_PAYLOAD, 0),
    JS_PROP_INT32_DEF("CLOSE_STATUS_POLICY_VIOLATION", LWS_CLOSE_STATUS_POLICY_VIOLATION, 0),
    JS_PROP_INT32_DEF("CLOSE_STATUS_MESSAGE_TOO_LARGE", LWS_CLOSE_STATUS_MESSAGE_TOO_LARGE, 0),
    JS_PROP_INT32_DEF("CLOSE_STATUS_EXTENSION_REQUIRED", LWS_CLOSE_STATUS_EXTENSION_REQUIRED, 0),
    JS_PROP_INT32_DEF("CLOSE_STATUS_UNEXPECTED_CONDITION", LWS_CLOSE_STATUS_UNEXPECTED_CONDITION, 0),
    JS_PROP_INT32_DEF("CLOSE_STATUS_TLS_FAILURE", LWS_CLOSE_STATUS_TLS_FAILURE, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_CONTINUE", HTTP_STATUS_CONTINUE, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_OK", HTTP_STATUS_OK, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_NO_CONTENT", HTTP_STATUS_NO_CONTENT, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_PARTIAL_CONTENT", HTTP_STATUS_PARTIAL_CONTENT, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_MOVED_PERMANENTLY", HTTP_STATUS_MOVED_PERMANENTLY, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_FOUND", HTTP_STATUS_FOUND, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_SEE_OTHER", HTTP_STATUS_SEE_OTHER, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_NOT_MODIFIED", HTTP_STATUS_NOT_MODIFIED, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_BAD_REQUEST", HTTP_STATUS_BAD_REQUEST, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_UNAUTHORIZED", HTTP_STATUS_UNAUTHORIZED, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_PAYMENT_REQUIRED", HTTP_STATUS_PAYMENT_REQUIRED, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_FORBIDDEN", HTTP_STATUS_FORBIDDEN, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_NOT_FOUND", HTTP_STATUS_NOT_FOUND, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_METHOD_NOT_ALLOWED", HTTP_STATUS_METHOD_NOT_ALLOWED, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_NOT_ACCEPTABLE", HTTP_STATUS_NOT_ACCEPTABLE, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_PROXY_AUTH_REQUIRED", HTTP_STATUS_PROXY_AUTH_REQUIRED, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_REQUEST_TIMEOUT", HTTP_STATUS_REQUEST_TIMEOUT, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_CONFLICT", HTTP_STATUS_CONFLICT, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_GONE", HTTP_STATUS_GONE, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_LENGTH_REQUIRED", HTTP_STATUS_LENGTH_REQUIRED, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_PRECONDITION_FAILED", HTTP_STATUS_PRECONDITION_FAILED, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_REQ_ENTITY_TOO_LARGE", HTTP_STATUS_REQ_ENTITY_TOO_LARGE, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_REQ_URI_TOO_LONG", HTTP_STATUS_REQ_URI_TOO_LONG, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE", HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_REQ_RANGE_NOT_SATISFIABLE", HTTP_STATUS_REQ_RANGE_NOT_SATISFIABLE, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_EXPECTATION_FAILED", HTTP_STATUS_EXPECTATION_FAILED, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_INTERNAL_SERVER_ERROR", HTTP_STATUS_INTERNAL_SERVER_ERROR, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_NOT_IMPLEMENTED", HTTP_STATUS_NOT_IMPLEMENTED, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_BAD_GATEWAY", HTTP_STATUS_BAD_GATEWAY, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_SERVICE_UNAVAILABLE", HTTP_STATUS_SERVICE_UNAVAILABLE, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_GATEWAY_TIMEOUT", HTTP_STATUS_GATEWAY_TIMEOUT, 0),
    JS_PROP_INT32_DEF("HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED", HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED, 0),
};

const size_t minnet_ws_proto_funcs_size = countof(minnet_ws_proto_funcs);
const size_t minnet_ws_static_funcs_size = countof(minnet_ws_static_funcs);
const size_t minnet_ws_proto_defs_size = countof(minnet_ws_proto_defs);

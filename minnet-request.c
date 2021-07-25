#include "minnet.h"
#include "minnet-websocket.h"
#include "minnet-request.h"

JSClassID minnet_request_class_id;
JSValue minnet_request_proto, minnet_request_ctor;

enum { REQUEST_METHOD, REQUEST_SOCKET, REQUEST_URI, REQUEST_PATH, REQUEST_HEADER, REQUEST_BODY };

void
minnet_request_dump(JSContext* ctx, MinnetRequest const* req) {
  printf("\nMinnetRequest {\n\turi = %s", req->url);
  printf("\n\tpath = %s", req->path);
  printf("\n\ttype = %s", req->type);

  buffer_dump("header", &req->header);
  fputs("\n\tresponse = ", stdout);
  fputs(" }", stdout);
  fflush(stdout);
}

void
minnet_request_init(JSContext* ctx, MinnetRequest* req, const char* in, struct socket* ws) {
  char buf[1024];
  ssize_t len;

  memset(req, 0, sizeof(*req));

  req->ref_count = 0;

  /* In contains the url part after the place the mount was  positioned at,
   * eg, if positioned at "/dyn" and given  "/dyn/mypath", in will contain /mypath */

  lws_snprintf(req->path, sizeof(req->path), "%s", (const char*)in);

  /*  if(lws_get_peer_simple(wsi, (char*)buf, sizeof(buf)))
      req->peer = js_strdup(ctx, buf);*/

  if((len = lws_hdr_copy(ws->lwsi, buf, sizeof(buf), WSI_TOKEN_GET_URI)) > 0) {
    req->url = js_strndup(ctx, buf, len);
    req->type = js_strdup(ctx, "GET");
  } else if((len = lws_hdr_copy(ws->lwsi, buf, sizeof(buf), WSI_TOKEN_POST_URI)) > 0) {
    req->url = js_strndup(ctx, buf, len);
    req->type = js_strdup(ctx, "POST");
  }

  if(!buffer_alloc(&req->header, LWS_RECOMMENDED_MIN_HEADER_SPACE, ctx))
    JS_ThrowOutOfMemory(ctx);
}

MinnetRequest*
request_new(JSContext* ctx, const char* in, struct socket* ws) {
  MinnetRequest* req;

  if((req = js_mallocz(ctx, sizeof(MinnetRequest))))
    minnet_request_init(ctx, req, in, ws);

  return req;
}

JSValue
minnet_request_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetRequest* req;
  int i;

  if(!(req = js_mallocz(ctx, sizeof(MinnetRequest))))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_request_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_request_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  if(argc >= 1) {
    if(JS_IsString(argv[0])) {
      const char* str = JS_ToCString(ctx, argv[0]);
      req->url = js_strdup(ctx, str);
      JS_FreeCString(ctx, str);
    }
  }

  if(argc >= 2) {
    if(JS_IsObject(argv[1]) && !JS_IsNull(argv[1])) {}
  }

  JS_SetOpaque(obj, req);

  return obj;

fail:
  js_free(ctx, req);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
minnet_request_new(JSContext* ctx, const char* in, struct socket* ws) {
  MinnetRequest* req;

  if(!(req = request_new(ctx, in, ws)))
    return JS_EXCEPTION;

  req->ref_count = 0;

  return minnet_request_wrap(ctx, req);
}

JSValue
minnet_request_wrap(JSContext* ctx, struct http_request* req) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_request_proto, minnet_request_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, req);

  ++req->ref_count;

  return ret;
}

static JSValue
minnet_request_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetRequest* req;
  if(!(req = JS_GetOpaque(this_val, minnet_request_class_id)))
    return JS_EXCEPTION;

  JSValue ret = JS_UNDEFINED;
  switch(magic) {
    case REQUEST_METHOD: {
      if(req->type)
        ret = JS_NewString(ctx, req->type);
      break;
    }
    case REQUEST_SOCKET: {
      /*if(req->ws)
        ret = minnet_ws_object(ctx, req->ws);*/
      break;
    }
    case REQUEST_URI: {
      if(req->url)
        ret = JS_NewString(ctx, req->url);
      break;
    }
    case REQUEST_PATH: {
      ret = JS_NewString(ctx, req->path);
      break;
    }
    case REQUEST_HEADER: {
      ret = buffer_tostring(&req->header, ctx);
      break;
    }
    case REQUEST_BODY: {
      ret = buffer_tobuffer(&req->header, ctx);

      break;
    }
  }
  return ret;
}

static void
minnet_request_finalizer(JSRuntime* rt, JSValue val) {
  MinnetRequest* req = JS_GetOpaque(val, minnet_request_class_id);
  if(req && --req->ref_count == 0) {
    if(req->url)
      js_free_rt(rt, req->url);

    js_free_rt(rt, req);
  }
}

JSClassDef minnet_request_class = {
    "MinnetRequest",
    .finalizer = minnet_request_finalizer,
};

const JSCFunctionListEntry minnet_request_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_request_get, 0, REQUEST_METHOD, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("url", minnet_request_get, 0, REQUEST_URI, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("path", minnet_request_get, 0, REQUEST_PATH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("socket", minnet_request_get, 0, REQUEST_SOCKET, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("header", minnet_request_get, 0, REQUEST_HEADER, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("body", minnet_request_get, 0, REQUEST_BODY, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetRequest", JS_PROP_CONFIGURABLE),
};

const size_t minnet_request_proto_funcs_size = countof(minnet_request_proto_funcs);

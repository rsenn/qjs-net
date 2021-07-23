#include "minnet.h"
#include "minnet-websocket.h"
#include "minnet-request.h"

JSClassID minnet_request_class_id;
JSValue minnet_request_proto;

void
minnet_request_dump(JSContext* ctx, MinnetRequest const* r) {
  printf("MinnetRequest\n\turi = %s", r->uri);
  printf("\n\tpath = %s", r->path);
  printf("\n\ttype = %s", r->type);
  printf("\n\tpeer = %s", r->peer);

  header_dump("header", &r->header);
  body_dump("body", &r->body);
  fputs("\n\tresponse = ", stdout);
  minnet_response_dump(ctx, &r->response);
  fputc('\n', stdout);
  fflush(stdout);
}

void
minnet_request_init(JSContext* ctx, MinnetRequest* r, const char* in, struct lws* wsi) {
  char buf[1024];
  ssize_t len;

  memset(r, 0, sizeof(*r));

  /* In contains the url part after the place the mount was  positioned at,
   * eg, if positioned at "/dyn" and given  "/dyn/mypath", in will contain /mypath */

  lws_snprintf(r->path, sizeof(r->path), "%s", (const char*)in);

  if((len = lws_get_peer_simple(wsi, (char*)buf, sizeof(buf))) > 0)
    r->peer = js_strndup(ctx, buf, len);

  if((len = lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_GET_URI)) > 0) {
    r->uri = js_strndup(ctx, buf, len);
    r->type = js_strdup(ctx, "GET");
  } else if((len = lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_POST_URI)) > 0) {
    r->uri = js_strndup(ctx, buf, len);
    r->type = js_strdup(ctx, "POST");
  }

  if(!header_alloc(ctx, &r->header, LWS_PRE + LWS_RECOMMENDED_MIN_HEADER_SPACE)) {
    JS_ThrowOutOfMemory(ctx);
  }

  minnet_response_zero(&r->response);
}

MinnetRequest*
minnet_request_new(JSContext* ctx, const char* in, struct lws* wsi) {
  MinnetRequest* r;
  if((r = js_malloc(ctx, sizeof(MinnetRequest))))
    minnet_request_init(ctx, r, in, wsi);
  return r;
}

JSValue
minnet_request_constructor(JSContext* ctx, const char* in, struct lws* wsi) {
  MinnetRequest* r;
  if(!(r = minnet_request_new(ctx, in, wsi)))
    return JS_EXCEPTION;

  return minnet_request_wrap(ctx, r);
}

JSValue
minnet_request_wrap(JSContext* ctx, struct http_request* req) {
  JSValue ret = JS_UNDEFINED;

  ret = JS_NewObjectProtoClass(ctx, minnet_request_proto, minnet_request_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;
  JS_SetOpaque(ret, req);
  return ret;
}

JSValue
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
    case REQUEST_PEER: {
      if(req->peer)
        ret = JS_NewString(ctx, req->peer);
      break;
    }
    case REQUEST_URI: {
      if(req->uri)
        ret = JS_NewString(ctx, req->uri);
      break;
    }
    case REQUEST_PATH: {
      ret = JS_NewString(ctx, req->path);
      break;
    }
    case REQUEST_HEADER: {
      ret = header_tostring(ctx, &req->header);
      break;
    }
    case REQUEST_BUFFER: {
      ret = header_tobuffer(ctx, &req->header);

      break;
    }
  }
  return ret;
}

JSValue
minnet_request_getter_path(JSContext* ctx, JSValueConst this_val) {
  MinnetRequest* req = JS_GetOpaque(this_val, minnet_request_class_id);
  if(req)
    return JS_NewString(ctx, req->path);

  return JS_EXCEPTION;
}

static void
minnet_request_finalizer(JSRuntime* rt, JSValue val) {
  MinnetRequest* req = JS_GetOpaque(val, minnet_request_class_id);
  if(req) {
    if(req->uri)
      js_free_rt(rt, req->uri);
  }
}

JSClassDef minnet_request_class = {
    "MinnetRequest",
    .finalizer = minnet_request_finalizer,
};

JSClassID minnet_request_class_id;

#define _GNU_SOURCE
#include "request.h"
//#include "lws-utils.h"
//#include "ringbuffer.h"
#include "headers.h"
//#include "jsutils.h"
#include <ctype.h>
#include <strings.h>
#include <libwebsockets.h>

Request* minnet_request_data(JSValueConst);

static const char* const method_names[] = {
    "GET",
    "POST",
    "OPTIONS",
    "PUT",
    "PATCH",
    "DELETE",
    "CONNECT",
    "HEAD",
};

const char*
method_string(enum http_method m) {
  if(m >= 0 && m < countof(method_names))
    return method_names[m];
  return 0;
}

int
method_number(const char* name) {
  int i = 0;
  if(name)
    for(i = countof(method_names) - 1; i >= 0; --i)
      if(!strcasecmp(name, method_names[i]))
        break;
  return i;
}

void
request_init(Request* req, URL url, enum http_method method) {
  // memset(req, 0, sizeof(Request));

  req->url = url;
  req->method = method;
  req->body = 0;
  req->ip = 0;
  req->read_only = FALSE;
  req->secure = url_is_tls(url);
}

Request*
request_alloc(JSContext* ctx) {
  Request* ret;

  ret = js_mallocz(ctx, sizeof(Request));
  ret->ref_count = 1;
  return ret;
}

Request*
request_new(URL url, HTTPMethod method, JSContext* ctx) {
  Request* req;

  if((req = request_alloc(ctx)))
    request_init(req, url, method);

  return req;
}

Request*
request_dup(Request* req) {
  ++req->ref_count;
  return req;
}

Request*
request_fromwsi(struct lws* wsi, JSContext* ctx) {
  Request* ret = 0;
  HTTPMethod method = wsi_method(wsi);
  URL url = URL_INIT();

  url_fromwsi(&url, wsi, ctx);

  ret = request_new(url, method, ctx);

  ret->ip = wsi_ipaddr(wsi);
  ret->secure = wsi_tls(wsi);
  ret->h2 = wsi_http2(wsi);

  return ret;
}

void
request_clear(Request* req, JSRuntime* rt) {
  url_free_rt(&req->url, rt);
  buffer_free(&req->headers);
  if(req->ip) {
    js_free_rt(rt, req->ip);
    req->ip = 0;
  }
  if(req->body)
    generator_destroy(&req->body);
}

void
request_free(Request* req, JSRuntime* rt) {
  if(--req->ref_count == 0) {
    request_clear(req, rt);
    js_free_rt(rt, req);
  }
}

Request*
request_from(int argc, JSValueConst argv[], JSContext* ctx) {
  Request* req = 0;
  URL url = {0, 0, 0, 0};

  if(JS_IsObject(argv[0]) && (req = minnet_request_data(argv[0]))) {
    req = request_dup(req);
  } else {
    url_fromvalue(&url, argv[0], ctx);

    if(url_valid(url))
      req = request_new(url, METHOD_GET, ctx);
  }

  if(req)
    if(argc >= 2 && JS_IsObject(argv[1])) {
      JSValue headers = JS_GetPropertyStr(ctx, argv[1], "headers");
      if(!JS_IsUndefined(headers))
        headers_fromobj(&req->headers, headers, "\r\n", ": ", ctx);

      JS_FreeValue(ctx, headers);
    }

  return req;
}

BOOL
request_match(Request* req, const char* path, enum http_method method) {

  if(strcmp(req->url.path, path))
    return FALSE;

  if(method != req->method)
    return FALSE;

  return TRUE;
}

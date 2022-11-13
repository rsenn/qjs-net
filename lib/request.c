#define _GNU_SOURCE
#include <quickjs.h>
#include <cutils.h>
#include "../minnet-request.h"
#include "ringbuffer.h"
#include "headers.h"
#include "jsutils.h"
#include <ctype.h>
#include <strings.h>
#include <libwebsockets.h>

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
request_format(struct http_request const* req, char* buf, size_t len, JSContext* ctx) {
  char* headers = buffer_escaped(&req->headers, ctx);
  char* url = url_format(req->url, ctx);
  snprintf(buf, len, FGC(196, "struct http_request") " { method: '%s', url: '%s', headers: '%s' }", method_name(req->method), url, headers);

  js_free(ctx, headers);
  js_free(ctx, url);
}

char*
request_dump(struct http_request const* req, JSContext* ctx) {
  static char buf[2048];
  request_format(req, buf, sizeof(buf), ctx);
  return buf;
}

void
request_init(struct http_request* req, struct url url, enum http_method method) {
  // memset(req, 0, sizeof(struct http_request));

  req->url = url;
  req->method = method;
  req->body = 0;
}

struct http_request*
request_alloc(JSContext* ctx) {
  struct http_request* ret;

  ret = js_mallocz(ctx, sizeof(struct http_request));
  ret->ref_count = 1;
  return ret;
}

struct http_request*
request_new(struct url url, HTTPMethod method, JSContext* ctx) {
  struct http_request* req;

  if((req = request_alloc(ctx)))
    request_init(req, url, method);

  return req;
}

struct http_request*
request_dup(struct http_request* req) {
  ++req->ref_count;
  return req;
}

struct http_request*
request_fromobj(JSValueConst options, JSContext* ctx) {
  struct http_request* req;
  JSValue value;
  const char *url, *path, *method;

  if(!(req = request_alloc(ctx)))
    return req;

  value = JS_GetPropertyStr(ctx, options, "url");
  url = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, options, "path");
  path = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  JS_GetPropertyStr(ctx, options, "method");
  method = JS_ToCString(ctx, value);
  JS_FreeValue(ctx, value);

  JS_GetPropertyStr(ctx, options, "headers");

  JS_FreeValue(ctx, value);

  request_init(req, /*path,*/ url_create(url, ctx), method_number(method));

  JS_FreeCString(ctx, url);
  JS_FreeCString(ctx, path);
  JS_FreeCString(ctx, method);

  return req;
}

struct http_request*
request_fromwsi(struct lws* wsi, JSContext* ctx) {
  struct http_request* ret = 0;
  HTTPMethod method = wsi_method(wsi);
  struct url url = URL_INIT();

  url_fromwsi(&url, wsi, ctx);

  ret = request_new(url, method, ctx);

  /*const char* uri;
    HTTPMethod method = -1;

    if((uri = wsi_uri_and_method(wsi, ctx, &method))) {
      struct url url = url_create(uri, ctx);
      struct lws_vhost* vhost;

      if((vhost = lws_get_vhost(wsi))) {
        const char* name;

        if((name = lws_get_vhost_name(vhost)))
          url_parse(&url, name, ctx);
      }

      ret = request_new(url, method, ctx);
    }

    if(ret && url_query(ret->url) == NULL) {
      char* q;
      size_t qlen;
      if((q = wsi_query_string_len(wsi, &qlen, ctx))) {
        url_set_query_len(&ret->url, q, qlen, ctx);
        js_free(ctx, q);
      }
    }
  */
  return ret;
}

struct http_request*
request_fromurl(const char* uri, JSContext* ctx) {
  HTTPMethod method = METHOD_GET;
  struct url url = url_create(uri, ctx);

  return request_new(url, method, ctx);
}

void
request_zero(struct http_request* req) {
  memset(req, 0, sizeof(struct http_request));
  req->headers = BUFFER_0();
  req->body = 0;
}

void
request_clear(struct http_request* req, JSContext* ctx) {
  url_free(&req->url, ctx);
  buffer_free_rt(&req->headers, JS_GetRuntime(ctx));
  if(req->body)
    generator_destroy(&req->body);
}

void
request_clear_rt(struct http_request* req, JSRuntime* rt) {
  url_free_rt(&req->url, rt);
  buffer_free_rt(&req->headers, rt);
  if(req->body)
    generator_destroy(&req->body);
}

void
request_free(struct http_request* req, JSContext* ctx) {
  if(--req->ref_count == 0) {
    request_clear(req, ctx);
    js_free(ctx, req);
  }
}

void
request_free_rt(struct http_request* req, JSRuntime* rt) {
  if(--req->ref_count == 0) {
    request_clear_rt(req, rt);
    js_free_rt(rt, req);
  }
}

struct http_request*
request_from(int argc, JSValueConst argv[], JSContext* ctx) {
  struct http_request* req = 0;
  struct url url = {0, 0, 0, 0};

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
        headers_fromobj(&req->headers, headers, ctx);

      JS_FreeValue(ctx, headers);
    }

  return req;
}

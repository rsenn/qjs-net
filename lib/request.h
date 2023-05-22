/**
 * @file request.h
 */
#ifndef QJSNET_LIB_REQUEST_H
#define QJSNET_LIB_REQUEST_H

#include "lws-utils.h"
#include "buffer.h"
#include "generator.h"
#include "url.h"

const char* method_string(enum http_method);
int method_number(const char*);

typedef struct http_request {
  int ref_count;
  BOOL read_only, secure, h2;
  enum http_method method;
  URL url;
  ByteBuffer headers;
  Generator* body;
  JSValue promise;
} Request;

const char* method_string(enum http_method);
int method_number(const char*);
void request_init(Request*, URL url, enum http_method method);
Request* request_alloc(JSContext*);
Request* request_new(URL, HTTPMethod method, JSContext* ctx);
Request* request_dup(Request*);
Request* request_fromwsi(struct lws*, JSContext* ctx);
void request_clear(Request*, JSRuntime* rt);
void request_free(Request*, JSRuntime* rt);
Request* request_from(int, JSValueConst argv[], JSContext* ctx);
BOOL request_match(Request*, const char* path, enum http_method method);

static inline JSValue
request_promise(Request* req, ResolveFunctions* fns, JSContext* ctx) {
  JSValue pr = js_async_create(ctx, fns);
  req->promise = pr;
  return pr;
}

static inline const char*
method_name(int m) {
  if(m < 0)
    return "-1";
  return ((const char* const[]){
      "GET",
      "POST",
      "OPTIONS",
      "PUT",
      "PATCH",
      "DELETE",
      "CONNECT",
      "HEAD",
  })[m];
}

#endif /* QJSNET_LIB_REQUEST_H */

#ifndef WJSNET_LIBQJSNET_LIB_REQUEST_H
#define WJSNET_LIBQJSNET_LIB_REQUEST_H

#include <quickjs.h>
#include <cutils.h>
#include "jsutils.h"
#include "../minnet-generator.h"
#include "url.h"

struct socket;
struct http_response;

const char* method_string(enum http_method);
int method_number(const char*);

struct http_request {
  int ref_count;
  BOOL read_only;
  enum http_method method;
  struct url url;
  ByteBuffer headers;
  MinnetGenerator* body;
};

const char* method_string(enum http_method);
int method_number(const char*);
void request_format(struct http_request const*, char* buf, size_t len, JSContext* ctx);
char* request_dump(struct http_request const*, JSContext* ctx);
void request_init(struct http_request*, struct url url, enum http_method method);
struct http_request* request_alloc(JSContext*);
struct http_request* request_new(struct url, HTTPMethod method, JSContext* ctx);
struct http_request* request_dup(struct http_request*);
struct http_request* request_fromobj(JSValueConst, JSContext* ctx);
struct http_request* request_fromwsi(struct lws*, JSContext* ctx);
struct http_request* request_fromurl(const char*, JSContext* ctx);
void request_zero(struct http_request*);
void request_clear(struct http_request*, JSContext* ctx);
void request_clear_rt(struct http_request*, JSRuntime* rt);
void request_free(struct http_request*, JSContext* ctx);
void request_free_rt(struct http_request*, JSRuntime* rt);
struct http_request* request_from(int, JSValueConst argv[], JSContext* ctx);

static inline const char*
method_name(int m) {
  if(m < 0)
    return "-1";
  return ((const char* const[]){"GET", "POST", "OPTIONS", "PUT", "PATCH", "DELETE", "CONNECT", "HEAD"})[m];
}
#endif /* WJSNET_LIBQJSNET_LIB_REQUEST_H */

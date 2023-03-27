#ifndef QJSNET_LIB_REQUEST_H
#define QJSNET_LIB_REQUEST_H

#include <quickjs.h>
#include <cutils.h>
#include "lws-utils.h"
#include "jsutils.h"
#include "generator.h"
#include "url.h"

struct socket;
struct http_response;

const char* method_string(enum http_method);
int method_number(const char*);

typedef struct http_request {
  int ref_count;
  BOOL read_only, secure, h2;
  enum http_method method;
  struct url url;
  ByteBuffer headers;
  Generator* body;
  char* ip;
} Request;

const char* method_string(enum http_method);
int method_number(const char*);

void request_init(Request*, struct url url, enum http_method method);
Request* request_alloc(JSContext*);
Request* request_new(struct url, HTTPMethod method, JSContext* ctx);
Request* request_dup(Request*);
Request* request_fromwsi(struct lws*, JSContext* ctx);
void request_clear_rt(Request*, JSRuntime* rt);
void request_free_rt(Request*, JSRuntime* rt);
Request* request_from(int, JSValueConst argv[], JSContext* ctx);

static inline const char*
method_name(int m) {
  if(m < 0)
    return "-1";
  return ((const char* const[]){"GET", "POST", "OPTIONS", "PUT", "PATCH", "DELETE", "CONNECT", "HEAD"})[m];
}

#endif /* QJSNET_LIB_REQUEST_H */

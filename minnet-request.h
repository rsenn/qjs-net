#ifndef MINNET_REQUEST_H
#define MINNET_REQUEST_H

#include <quickjs.h>
#include <cutils.h>
#include "jsutils.h"
#include "minnet-generator.h"
#include "minnet-url.h"

struct socket;
struct http_response;

const char* method_string(enum http_method);
int method_number(const char*);

typedef struct http_request {
  int ref_count;
  BOOL read_only;
  enum http_method method;
  MinnetURL url;
  ByteBuffer headers;
  MinnetGenerator* body;
} MinnetRequest;

void request_format(MinnetRequest const*, char*, size_t, JSContext* ctx);
char* request_dump(MinnetRequest const*, JSContext*);
void request_init(MinnetRequest*, MinnetURL, enum http_method);
MinnetRequest* request_new(MinnetURL, HTTPMethod method, JSContext*);
MinnetRequest* request_dup(MinnetRequest*);
MinnetRequest* request_fromobj(JSValueConst, JSContext*);
MinnetRequest* request_fromwsi(struct lws*, JSContext*);
MinnetRequest* request_fromurl(const char* uri, JSContext* ctx);
void request_zero(MinnetRequest*);
void request_clear(MinnetRequest*, JSContext*);
void request_clear_rt(MinnetRequest*, JSRuntime*);
void request_free(MinnetRequest*, JSContext*);
void request_free_rt(MinnetRequest*, JSRuntime*);
MinnetRequest* request_from(int argc, JSValueConst argv[], JSContext*);
JSValue minnet_request_from(JSContext*, int argc, JSValueConst argv[]);
JSValue minnet_request_constructor(JSContext*, JSValueConst, int, JSValueConst argv[]);
JSValue minnet_request_new(JSContext*, MinnetURL, enum http_method);
JSValue minnet_request_wrap(JSContext*, MinnetRequest*);

extern THREAD_LOCAL JSValue minnet_request_proto, minnet_request_ctor;
extern THREAD_LOCAL JSClassID minnet_request_class_id;
extern JSClassDef minnet_request_class;
extern const JSCFunctionListEntry minnet_request_proto_funcs[];
extern const size_t minnet_request_proto_funcs_size;

static inline MinnetRequest*
minnet_request_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_request_class_id);
}

static inline MinnetRequest*
minnet_request_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_request_class_id);
}

static inline const char*
method_name(int m) {
  if(m < 0)
    return "-1";
  return ((const char* const[]){"GET", "POST", "OPTIONS", "PUT", "PATCH", "DELETE", "CONNECT", "HEAD"})[m];
}
#endif /* MINNET_REQUEST_H */

#ifndef MINNET_REQUEST_H
#define MINNET_REQUEST_H

#include <quickjs.h>
#include <cutils.h>
#include "minnet.h"
#include "minnet-buffer.h"
#include "minnet-url.h"

struct socket;
struct http_response;

enum http_method { METHOD_GET = 0, METHOD_POST, METHOD_OPTIONS, METHOD_PUT, METHOD_PATCH, METHOD_DELETE, METHOD_CONNECT, METHOD_HEAD };

typedef enum http_method MinnetHttpMethod;

const char* method_string(enum http_method);
int method_number(const char*);

typedef struct http_request {
  int ref_count;
  BOOL read_only;
  enum http_method method;
  MinnetURL url;
  MinnetBuffer headers, body;
  // char path[256];
} MinnetRequest;

void request_format(MinnetRequest const*, char*, size_t, JSContext* ctx);
char* request_dump(MinnetRequest const*, JSContext*);
void request_init(MinnetRequest*, MinnetURL, enum http_method);
MinnetRequest* request_new(JSContext*, const char*, MinnetURL, MinnetHttpMethod method);
MinnetRequest* request_dup(MinnetRequest*);
MinnetRequest* request_from(JSContext*, JSValueConst);
void request_zero(MinnetRequest*);
JSValue minnet_request_from(JSContext*, JSValueConst);
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

static inline char*
lws_uri_and_method(struct lws* wsi, JSContext* ctx, MinnetHttpMethod* method) {
  char* url;

  if((url = lws_get_uri(wsi, ctx, WSI_TOKEN_POST_URI)))
    *method = METHOD_POST;
  else if((url = lws_get_uri(wsi, ctx, WSI_TOKEN_GET_URI)))
    *method = METHOD_GET;
  else if((url = lws_get_uri(wsi, ctx, WSI_TOKEN_HEAD_URI)))
    *method = METHOD_HEAD;
  else if((url = lws_get_uri(wsi, ctx, WSI_TOKEN_OPTIONS_URI)))
    *method = METHOD_OPTIONS;

  return url;
}

static inline const char*
method_name(int m) {
  if(m < 0)
    return "-1";
  return ((const char* const[]){"GET", "POST", "OPTIONS", "PUT", "PATCH", "DELETE", "CONNECT", "HEAD"})[m];
}

#endif /* MINNET_REQUEST_H */

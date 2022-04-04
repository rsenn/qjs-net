#ifndef MINNET_REQUEST_H
#define MINNET_REQUEST_H

#include <quickjs.h>
#include <cutils.h>
#include "minnet.h"
#include "minnet-buffer.h"

struct socket;
struct http_response;

const char* method_string(enum http_method);
int method_number(const char*);

typedef struct http_request {
  int ref_count;
  BOOL read_only;
  enum http_method method;
  MinnetURL url;
  MinnetBuffer headers, body;
} MinnetRequest;

void request_format(MinnetRequest const*, char*, size_t, JSContext* ctx);
char* request_dump(MinnetRequest const*, JSContext*);
void request_init(MinnetRequest*, MinnetURL, enum http_method);
MinnetRequest* request_new(JSContext*, MinnetURL, MinnetHttpMethod method);
MinnetRequest* request_dup(MinnetRequest*);
MinnetRequest* request_fromobj(JSContext*, JSValueConst);
MinnetRequest* request_fromwsi(JSContext*, struct lws*);
MinnetRequest* request_fromurl(JSContext* ctx, const char* uri);
void request_zero(MinnetRequest*);
void request_clear(MinnetRequest*, JSContext*);
void request_clear_rt(MinnetRequest*, JSRuntime*);
void request_free(MinnetRequest*, JSContext*);
void request_free_rt(MinnetRequest*, JSRuntime*);
MinnetRequest* request_from(JSContext*, int argc, JSValueConst argv[]);
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

static inline char*
minnet_uri_and_method(struct lws* wsi, JSContext* ctx, MinnetHttpMethod* method) {
  char* url;

  if((url = lws_get_uri(wsi, ctx, WSI_TOKEN_POST_URI)))
    if(method)
      *method = METHOD_POST;
    else if((url = lws_get_uri(wsi, ctx, WSI_TOKEN_GET_URI)))
      if(method)
        *method = METHOD_GET;
      else if((url = lws_get_uri(wsi, ctx, WSI_TOKEN_HEAD_URI)))
        if(method)
          *method = METHOD_HEAD;
        else if((url = lws_get_uri(wsi, ctx, WSI_TOKEN_OPTIONS_URI)))
          if(method)
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

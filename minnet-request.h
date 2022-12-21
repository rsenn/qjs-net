#ifndef MINNET_REQUEST_H
#define MINNET_REQUEST_H

#include "request.h"
#include "minnet-url.h"

typedef struct http_request MinnetRequest;

MinnetRequest* minnet_request_data(JSValueConst);
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
minnet_request_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_request_class_id);
}

#endif /* MINNET_REQUEST_H */

#ifndef MINNET_RESPONSE_H
#define MINNET_RESPONSE_H

#include "response.h"
#include "minnet-url.h"

typedef struct http_response MinnetResponse;

MinnetResponse* minnet_response_data(JSValueConst);
JSValue minnet_response_new(JSContext*, MinnetURL, int, char* status_text, BOOL headers_sent, const char* type);
JSValue minnet_response_wrap(JSContext*, MinnetResponse*);
JSValue minnet_response_constructor(JSContext*, JSValue, int, JSValue argv[]);
void minnet_response_finalizer(JSRuntime*, JSValue);

extern THREAD_LOCAL JSClassID minnet_response_class_id;
extern THREAD_LOCAL JSValue minnet_response_proto, minnet_response_ctor;
extern JSClassDef minnet_response_class;
extern const JSCFunctionListEntry minnet_response_proto_funcs[];
extern const size_t minnet_response_proto_funcs_size;

static inline MinnetResponse*
minnet_response_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_response_class_id);
}
#endif /* MINNET_RESPONSE_H */

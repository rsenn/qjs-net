#ifndef MINNET_RESPONSE_H
#define MINNET_RESPONSE_H

#include <quickjs.h>
#include <list.h>
#include <libwebsockets.h>
#include "minnet.h"
#include "minnet-buffer.h"
#include "minnet-url.h"

struct http_request;

/* class MinnetResponse */

/*typedef struct http_header {
  char *name, *value;
  struct list_head link;
} MinnetHttpHeader;*/

typedef struct http_response {
  int ref_count;
  BOOL read_only;
  MinnetURL url;
  char* type;
  int status;
  BOOL ok;
  MinnetBuffer headers, body;
} MinnetResponse;

void response_format(MinnetResponse const*, char*, size_t);
char* response_dump(MinnetResponse const*);
void response_zero(MinnetResponse*);
void response_init(MinnetResponse*, MinnetURL, int32_t, BOOL ok, char* type);
MinnetResponse* response_dup(MinnetResponse*);
ssize_t response_write(MinnetResponse*, const void*, size_t, JSContext* ctx);
void response_clear(MinnetResponse*, JSContext*);
void response_clear_rt(MinnetResponse*, JSRuntime*);
void response_free(MinnetResponse*, JSContext*);
void response_free_rt(MinnetResponse*, JSRuntime*);
MinnetResponse* response_new(JSContext*);
JSValue minnet_response_new(JSContext*, MinnetURL, int32_t, BOOL ok, const char* type);
JSValue minnet_response_wrap(JSContext*, MinnetResponse*);
JSValue minnet_response_constructor(JSContext*, JSValueConst, int, JSValueConst argv[]);
void minnet_response_finalizer(JSRuntime*, JSValueConst);

extern THREAD_LOCAL JSClassID minnet_response_class_id;
extern THREAD_LOCAL JSValue minnet_response_proto, minnet_response_ctor;
extern JSClassDef minnet_response_class;
extern const JSCFunctionListEntry minnet_response_proto_funcs[];
extern const size_t minnet_response_proto_funcs_size;

static inline MinnetResponse*
minnet_response_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_response_class_id);
}

static inline MinnetResponse*
minnet_response_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_response_class_id);
}

#endif /* MINNET_RESPONSE_H */

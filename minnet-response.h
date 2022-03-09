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
  BOOL read_only;
  MinnetURL url;
  char* type;
  int status;
  BOOL ok;
  MinnetBuffer headers, body;
} MinnetResponse;

struct http_header* header_new(JSContext*, const char* name, const char* value);
void header_free(JSRuntime*, struct http_header* hdr);
char* response_dump(struct http_response const*);
void response_zero(struct http_response*);
void response_init(struct http_response*, MinnetURL, int32_t status, BOOL ok, char* type);
ssize_t response_write(struct http_response*, const void* x, size_t n, JSContext* ctx);
void response_free(JSRuntime*, struct http_response* res);
struct http_response* response_new(JSContext*);
JSValue minnet_response_new(JSContext*, MinnetURL, int32_t status, BOOL ok, const char* type);
JSValue minnet_response_wrap(JSContext*, struct http_response* res);
JSValue minnet_response_constructor(JSContext*, JSValue new_target, int argc, JSValue argv[]);
void minnet_response_finalizer(JSRuntime*, JSValue val);

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

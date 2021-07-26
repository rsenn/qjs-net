#ifndef MINNET_REQUEST_H
#define MINNET_REQUEST_H

#include <quickjs.h>
#include <cutils.h>
#include "buffer.h"

struct socket;
struct http_response;

typedef struct http_request {
  int ref_count;
  BOOL read_only;
  char *type, *url;
  struct byte_buffer header, body;
  char path[256];
} MinnetRequest;

void request_dump(struct http_request const*);
void request_init(struct http_request*, const char* path, char* url, char* method);
struct http_request* request_new(JSContext*);
void request_zero(struct http_request*);
JSValue minnet_request_constructor(JSContext*, JSValue new_target, int argc, JSValue argv[]);
JSValue minnet_request_new(JSContext*, const char* in, struct socket* ws);
JSValue minnet_request_wrap(JSContext*, struct http_request* req);

extern JSClassDef minnet_request_class;
extern JSValue minnet_request_proto, minnet_request_ctor;
extern JSClassID minnet_request_class_id;
extern const JSCFunctionListEntry minnet_request_proto_funcs[];
extern const size_t minnet_request_proto_funcs_size;

static inline MinnetRequest*
minnet_request_data(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_request_class_id);
}

#endif /* MINNET_REQUEST_H */

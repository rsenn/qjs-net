#ifndef MINNET_REQUEST_H
#define MINNET_REQUEST_H

#include <quickjs.h>
#include "buffer.h"

struct http_response;

typedef struct http_request {
  int ref_count;
  char *type, *url;
  struct lws* ws;
  struct byte_buffer header;
  char path[256];
  struct http_response* response;
} MinnetRequest;

void minnet_request_dump(JSContext*,  struct http_request const* req);
void minnet_request_init(JSContext*,  struct http_request* req, const char* in, struct lws* wsi);
MinnetRequest* minnet_request_new(JSContext*, const char* in, struct lws* wsi);
JSValue minnet_request_wrap(JSContext*, struct http_request* req);

extern JSClassDef minnet_request_class;
extern JSValue minnet_request_proto;
extern JSClassID minnet_request_class_id;

extern const JSCFunctionListEntry minnet_request_proto_funcs[];
extern const size_t minnet_request_proto_funcs_size;

#endif /* MINNET_REQUEST_H */

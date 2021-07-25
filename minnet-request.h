#ifndef MINNET_REQUEST_H
#define MINNET_REQUEST_H

#include <quickjs.h>
#include "minnet-response.h"
#include "buffer.h"

typedef struct http_request {
  int ref_count;
  char *type, *url;
  struct lws* ws;
  struct byte_buffer header;
  char path[256];
  MinnetResponse response;
} MinnetRequest;

void minnet_request_dump(JSContext*, MinnetRequest const* r);
void minnet_request_init(JSContext*, MinnetRequest* r, const char* in, struct lws* wsi);
MinnetRequest* minnet_request_new(JSContext*, const char* in, struct lws* wsi);
JSValue minnet_request_constructor(JSContext*, const char* in, struct lws* wsi);
JSValue minnet_request_wrap(JSContext*, struct http_request* req);
JSValue minnet_request_get(JSContext*, JSValue this_val, int magic);

extern JSClassDef minnet_request_class;
extern JSValue minnet_request_proto;
extern JSClassID minnet_request_class_id;

enum { REQUEST_METHOD, REQUEST_SOCKET, REQUEST_URI, REQUEST_PATH, REQUEST_HEADER, REQUEST_BUFFER };

extern const JSCFunctionListEntry minnet_request_proto_funcs[];
extern const size_t minnet_request_proto_funcs_size;

#endif /* MINNET_REQUEST_H */

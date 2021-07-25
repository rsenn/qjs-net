#ifndef MINNET_REQUEST_H
#define MINNET_REQUEST_H

#include <quickjs.h>
#include "buffer.h"

struct socket;
struct http_response;

typedef struct http_request {
  int ref_count;
  char *type, *url;
  struct socket* ws;
  struct byte_buffer header;
  char path[256];
  struct http_response* rsp;
} MinnetRequest;

void minnet_request_dump(JSContext*, MinnetRequest const* req);
void minnet_request_init(JSContext*, MinnetRequest* req, const char* in, struct socket* ws);
MinnetRequest* minnet_request_new(JSContext*, const char* in, struct socket* ws);
JSValue minnet_request_wrap(JSContext*, struct http_request* req);

extern JSClassDef minnet_request_class;
extern JSValue minnet_request_proto;
extern JSClassID minnet_request_class_id;

extern const JSCFunctionListEntry minnet_request_proto_funcs[];
extern const size_t minnet_request_proto_funcs_size;

#endif /* MINNET_REQUEST_H */

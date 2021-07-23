#ifndef MINNET_REQUEST_H
#define MINNET_REQUEST_H

#include "quickjs.h"
#include "minnet-response.h"

typedef struct http_header {
  uint8_t *start, *pos, *end;
} MinnetHttpHeader;

typedef struct http_body {
  size_t times, budget /*, content_lines*/;
} MinnetHttpBody;

typedef struct http_request {
  char *peer, *type, *uri;
  struct http_header header;
  struct http_body body;
  char path[256];
  MinnetResponse response;
} MinnetRequest;

void minnet_request_dump(MinnetRequest const*);
void minnet_request_init(JSContext*, MinnetRequest* r, const char* in, struct lws* wsi);
MinnetRequest* minnet_request_new(JSContext*, const char* in, struct lws* wsi);
JSValue minnet_request_constructor(JSContext*, const char* in, struct lws* wsi);
JSValue minnet_request_wrap(JSContext*, struct http_request* req);
JSValue minnet_request_get(JSContext*, JSValue this_val, int magic);
JSValue minnet_request_getter_path(JSContext*, JSValue this_val);

extern JSClassDef minnet_request_class;
extern JSValue minnet_request_proto;
extern JSClassID minnet_request_class_id;

enum { REQUEST_METHOD, REQUEST_PEER, REQUEST_URI, REQUEST_PATH, REQUEST_HEADER, REQUEST_BUFFER };

static const JSCFunctionListEntry minnet_request_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("type", minnet_request_get, 0, REQUEST_METHOD, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("url", minnet_request_get, 0, REQUEST_URI, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("path", minnet_request_get, 0, REQUEST_PATH, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("peer", minnet_request_get, 0, REQUEST_PEER, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("header", minnet_request_get, 0, REQUEST_HEADER, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("buffer", minnet_request_get, 0, REQUEST_BUFFER, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetRequest", JS_PROP_CONFIGURABLE),
};

#endif /* MINNET_REQUEST_H */
#ifndef MINNET_RESPONSE_H
#define MINNET_RESPONSE_H

#include "quickjs.h"

/* class Response */

typedef struct http_response {
  uint8_t* buffer;
  long size;
  JSValue status;
  JSValue ok;
  JSValue url;
  JSValue type;
} MinnetResponse;

void minnet_response_dump(JSContext*, struct http_response const* res);
void minnet_response_zero(struct http_response*);
void minnet_response_init(JSContext*, MinnetResponse* res, int32_t status, BOOL ok, const char* url, const char* type);
void minnet_response_free(JSRuntime*, MinnetResponse* res);
JSValue minnet_response_new(JSContext*, int32_t status, BOOL ok, const char* url, const char* type, uint8_t* buf, size_t len);
JSValue minnet_response_wrap(JSContext*, MinnetResponse* res);
JSValue minnet_response_buffer(JSContext*, JSValue this_val, int argc, JSValue* argv);
JSValue minnet_response_json(JSContext*, JSValue this_val, int argc, JSValue* argv);
JSValue minnet_response_text(JSContext*, JSValue this_val, int argc, JSValue* argv);
JSValue minnet_response_getter_ok(JSContext*, JSValue this_val, int magic);
JSValue minnet_response_getter_url(JSContext*, JSValue this_val, int magic);
JSValue minnet_response_getter_status(JSContext*, JSValue this_val, int magic);
JSValue minnet_response_getter_type(JSContext*, JSValue this_val, int magic);
void minnet_response_finalizer(JSRuntime*, JSValue val);

extern JSClassDef minnet_response_class;

static const JSCFunctionListEntry minnet_response_proto_funcs[] = {
    JS_CFUNC_DEF("arrayBuffer", 0, minnet_response_buffer),
    JS_CFUNC_DEF("json", 0, minnet_response_json),
    JS_CFUNC_DEF("text", 0, minnet_response_text),
    JS_CGETSET_FLAGS_DEF("ok", minnet_response_getter_ok, NULL, JS_PROP_ENUMERABLE),
    JS_CGETSET_FLAGS_DEF("url", minnet_response_getter_url, NULL, JS_PROP_ENUMERABLE),
    JS_CGETSET_FLAGS_DEF("status", minnet_response_getter_status, NULL, JS_PROP_ENUMERABLE),
    JS_CGETSET_FLAGS_DEF("type", minnet_response_getter_type, NULL, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetResponse", JS_PROP_CONFIGURABLE),
};

extern JSValue minnet_response_proto;
extern JSClassID minnet_response_class_id;

#endif /* MINNET_RESPONSE_H */

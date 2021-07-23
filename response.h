#ifndef MINNET_RESPONSE_H
#define MINNET_RESPONSE_H

#include "quickjs.h"

/* class Response */

typedef struct {
  uint8_t* buffer;
  long size;
  JSValue status;
  JSValue ok;
  JSValue url;
  JSValue type;
} MinnetResponse;

JSValue minnet_response_buffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue minnet_response_json(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue minnet_response_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue minnet_response_getter_ok(JSContext* ctx, JSValueConst this_val);
JSValue minnet_response_getter_url(JSContext* ctx, JSValueConst this_val);
JSValue minnet_response_getter_status(JSContext* ctx, JSValueConst this_val);
JSValue minnet_response_getter_type(JSContext* ctx, JSValueConst this_val);
static void minnet_response_finalizer(JSRuntime* rt, JSValue val);

extern JSClassDef minnet_response_class;

static const JSCFunctionListEntry minnet_response_proto_funcs[] = {
    JS_CFUNC_DEF("arrayBuffer", 0, minnet_response_buffer),
    JS_CFUNC_DEF("json", 0, minnet_response_json),
    JS_CFUNC_DEF("text", 0, minnet_response_text),
    JS_CGETSET_DEF("ok", minnet_response_getter_ok, NULL),
    JS_CGETSET_DEF("url", minnet_response_getter_url, NULL),
    JS_CGETSET_DEF("status", minnet_response_getter_status, NULL),
    JS_CGETSET_DEF("type", minnet_response_getter_type, NULL),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetResponse", JS_PROP_CONFIGURABLE),
};

extern JSClassID minnet_response_class_id;

#endif /* MINNET_RESPONSE_H */
#include "minnet-generator.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

THREAD_LOCAL JSClassID minnet_generator_class_id;
THREAD_LOCAL JSValue minnet_generator_proto, minnet_generator_ctor;
 

JSValue
minnet_generator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
 /* MinnetGenerator* strm;

  if(!(strm = generator_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);*/

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_generator_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_generator_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  
//  JS_SetOpaque(obj, strm);

  return obj;

fail:
  //js_free(ctx, strm);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}
 

JSValue
minnet_generator_wrap(JSContext* ctx, struct generator* strm) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_generator_proto, minnet_generator_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, strm);

  ++strm->ref_count;

  return ret;
}

enum { GENERATOR_TYPE, GENERATOR_LENGTH, GENERATOR_AVAIL, GENERATOR_BUFFER, GENERATOR_TEXT };

static JSValue
minnet_generator_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetGenerator* strm;
  if(!(strm = JS_GetOpaque2(ctx, this_val, minnet_generator_class_id)))
    return JS_EXCEPTION;

  JSValue ret = JS_UNDEFINED;
  switch(magic) {
 }
  return ret;
}

static JSValue
minnet_generator_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

static JSValue
minnet_generator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  MinnetGenerator* strm;
  JSValue ret = JS_UNDEFINED;
  size_t len;
  uint8_t* ptr;

  if(!(strm = minnet_generator_data(ctx, this_val)))
    return JS_EXCEPTION;
 

  return ret;
}

static void
minnet_generator_finalizer(JSRuntime* rt, JSValue val) {
  MinnetGenerator* strm = JS_GetOpaque(val, minnet_generator_class_id);
  if(strm && --strm->ref_count == 0) {

    // buffer_free(&strm->buffer, rt);

    js_free_rt(rt, strm);
  }
}

JSClassDef minnet_generator_class = {
    "MinnetGenerator",
    .finalizer = minnet_generator_finalizer,
};

const JSCFunctionListEntry minnet_generator_proto_funcs[] = {
       JS_ITERATOR_NEXT_DEF("next", 0, minnet_generator_next, 0),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, minnet_generator_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetGenerator", JS_PROP_CONFIGURABLE),
};

const size_t minnet_generator_proto_funcs_size = countof(minnet_generator_proto_funcs);

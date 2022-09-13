#include "minnet-generator.h"
#include "jsutils.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>

THREAD_LOCAL JSClassID minnet_generator_class_id;
THREAD_LOCAL JSValue minnet_generator_proto, minnet_generator_ctor;

static JSValue
minnet_generator_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  return JS_DupValue(ctx, this_val);
}

static const JSCFunctionListEntry minnet_generator_funcs[] = {
    JS_CFUNC_DEF("[Symbol.asyncIterator]", 0, minnet_generator_iterator),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetGenerator", JS_PROP_CONFIGURABLE),
};

static JSValue
minnet_generator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, void* opaque) {
  MinnetGenerator* gen = (MinnetGenerator*)opaque;
  JSValue ret = JS_UNDEFINED;

  ret = generator_next(gen, ctx);

  return ret;
}

JSValue
minnet_generator_create(JSContext* ctx, MinnetGenerator** gen_p) {
  JSValue ret = JS_NewObject(ctx);

  if(!*gen_p)
    *gen_p = generator_new(ctx);
  else
    generator_dup(*gen_p);

  JS_SetPropertyStr(ctx, ret, "next", JS_NewCClosure(ctx, minnet_generator_next, 0, 0, *gen_p, (void*)&generator_free));
  JS_SetPropertyFunctionList(ctx, ret, minnet_generator_funcs, countof(minnet_generator_funcs));

  return ret;
}

/*
JSValue
minnet_generator_wrap(JSContext* ctx, MinnetGenerator* gen) {
  JSValue ret = JS_NewObject(ctx);

    ++gen->ref_count;

  JS_SetPropertyStr(ctx, ret, "next", JS_NewCClosure(ctx, minnet_generator_next, 0, 0, gen_p, (void*)&generator_free));

  return ret;
}
*/

#include "minnet-generator.h"
#include "jsutils.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>

THREAD_LOCAL JSClassID minnet_generator_class_id;
THREAD_LOCAL JSValue minnet_generator_proto, minnet_generator_ctor;

JSClassDef minnet_generator_class = {
    "MinnetGenerator",
    //.finalizer = minnet_generator_finalizer,
};

static const JSCFunctionListEntry minnet_generator_funcs[] = {
    JS_CFUNC_DEF("[Symbol.asyncIterator]", 0, (JSCFunction*)&JS_DupValue),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetGenerator", JS_PROP_CONFIGURABLE),
};

static JSValue
minnet_generator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, void* opaque) {
  MinnetGenerator* gen = (MinnetGenerator*)opaque;
  JSValue ret = JS_UNDEFINED;

  ret = generator_next(gen);

  return ret;
}

static JSValue
minnet_generator_push(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, void* opaque) {
  MinnetGenerator* gen = (MinnetGenerator*)opaque;
  JSValue ret = JS_UNDEFINED;

  if(argc < 1)
    return JS_ThrowInternalError(ctx, "argument required");

  JSValue callback = argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_NULL;

  ret = generator_push(gen, argv[0]);
  JS_FreeValue(ctx, callback);
  return ret;
}

static JSValue
minnet_generator_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, void* opaque) {
  MinnetGenerator* gen = (MinnetGenerator*)opaque;
  JSValue ret = JS_UNDEFINED;

  JSValue callback = argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_NULL;

  BOOL result = generator_close(gen, callback);

  JS_FreeValue(ctx, callback);

  return ret;
}

JSValue
minnet_generator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  MinnetGenerator* gen;
  JSValue ret, args[2];

  if(!(gen = generator_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  if(argc < 1 || !JS_IsFunction(ctx, argv[0]))
    return JS_ThrowInternalError(ctx, "MinnetGenerator needs a function parameter");

  args[0] = JS_NewCClosure(ctx, minnet_generator_push, 0, 0, generator_dup(gen), (void*)&generator_free);
  args[1] = JS_NewCClosure(ctx, minnet_generator_stop, 0, 0, generator_dup(gen), (void*)&generator_free);

  ret = JS_Call(ctx, argv[0], JS_UNDEFINED, 2, args);

  JS_FreeValue(ctx, ret);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);

  return minnet_generator_iterator(ctx, gen);
}

JSValue
minnet_generator_iterator(JSContext* ctx, MinnetGenerator* gen) {
  JSValue ret = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, ret, "next", JS_NewCClosure(ctx, minnet_generator_next, 0, 0, generator_dup(gen), (void*)&generator_free));
  JS_SetPropertyFunctionList(ctx, ret, minnet_generator_funcs, countof(minnet_generator_funcs));

  return ret;
}

JSValue
minnet_generator_reader(JSContext* ctx, MinnetGenerator* gen) {
  JSValue ret = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, ret, "read", JS_NewCClosure(ctx, minnet_generator_next, 0, 0, generator_dup(gen), (void*)&generator_free));
  JS_SetPropertyFunctionList(ctx, ret, minnet_generator_funcs, countof(minnet_generator_funcs));

  return ret;
}

JSValue
minnet_generator_create(JSContext* ctx, MinnetGenerator** gen_p) {
  if(!*gen_p)
    *gen_p = generator_new(ctx);
  else
    generator_dup(*gen_p);

  return minnet_generator_iterator(ctx, *gen_p);
}

/*
JSValue
minnet_generator_iterator(JSContext* ctx, MinnetGenerator* gen) {
  JSValue ret = JS_NewObject(ctx);

    ++gen->ref_count;

  JS_SetPropertyStr(ctx, ret, "next", JS_NewCClosure(ctx, minnet_generator_next, 0, 0, gen_p, (void*)&generator_free));

  return ret;
}
*/

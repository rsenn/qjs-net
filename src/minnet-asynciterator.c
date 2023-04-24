#include "minnet-asynciterator.h"
#include "jsutils.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>

THREAD_LOCAL JSClassID minnet_asynciterator_class_id;
THREAD_LOCAL JSValue minnet_asynciterator_proto, minnet_asynciterator_ctor;

static JSValue
minnet_asynciterator_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  AsyncIterator* iter;

  if(!(iter = minnet_asynciterator_data2(ctx, this_val)))
    return JS_EXCEPTION;

  return asynciterator_next(iter, ctx);
}

static JSValue
minnet_asynciterator_push(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[] /*, int magic*/) {
  AsyncIterator* iter;

  if(!(iter = minnet_asynciterator_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(argc < 1)
    return JS_ThrowInternalError(ctx, "argument required");

  return JS_NewBool(ctx, asynciterator_yield(iter, argv[0], ctx));
}

static JSValue
minnet_asynciterator_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[] /*, int magic*/) {
  AsyncIterator* iter;

  if(!(iter = minnet_asynciterator_data2(ctx, this_val)))
    return JS_EXCEPTION;

  return JS_NewBool(ctx, asynciterator_stop(iter, ctx));
}

JSValue
minnet_asynciterator_wrap(JSContext* ctx, AsyncIterator* iter) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_asynciterator_proto, minnet_asynciterator_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  // printf("%s() iter=%p\n", __func__, iter);

  JS_SetOpaque(ret, asynciterator_dup(iter));

  return ret;
}

static JSValue
minnet_asynciterator_asynciterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

void
minnet_asynciterator_decorate(JSContext* ctx, JSValueConst this_val, JSValueConst ret) {
  JSValue fn = JS_NewCFunction(ctx, minnet_asynciterator_asynciterator, "[Symbol.asyncIterator]", 0);

  JSAtom atom = js_symbol_static_atom(ctx, "asyncIterator");
  JS_SetProperty(ctx, ret, atom, js_function_bind_this(ctx, fn, this_val));
  JS_FreeAtom(ctx, atom);
  JS_FreeValue(ctx, fn);
}

JSValue
minnet_asynciterator_iterable(JSContext* ctx, JSValueConst this_val) {
  JSValue ret = JS_NewObject(ctx);

  minnet_asynciterator_decorate(ctx, this_val, ret);

  return ret;
}

JSValue
minnet_asynciterator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  AsyncIterator* iter;

  if(!(iter = asynciterator_new(ctx)))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_asynciterator_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_asynciterator_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, iter);

  return obj;

fail:
  js_free(ctx, iter);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static void
minnet_asynciterator_finalizer(JSRuntime* rt, JSValue val) {
  AsyncIterator* iter;

  if((iter = minnet_asynciterator_data(val)))
    asynciterator_free(iter, rt);
}

JSClassDef minnet_asynciterator_class = {
    "AsyncIterator",
    .finalizer = minnet_asynciterator_finalizer,
};

const JSCFunctionListEntry minnet_asynciterator_proto_funcs[] = {
    JS_CFUNC_DEF("next", 0, minnet_asynciterator_next),
    JS_CFUNC_DEF("push", 0, minnet_asynciterator_push),
    JS_CFUNC_DEF("stop", 0, minnet_asynciterator_stop),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AsyncIterator", JS_PROP_CONFIGURABLE),
};

const size_t minnet_asynciterator_proto_funcs_size = countof(minnet_asynciterator_proto_funcs);

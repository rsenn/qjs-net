#include "minnet-asynciterator.h"
#include "js-utils.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>

THREAD_LOCAL JSClassID minnet_asynciterator_class_id;
THREAD_LOCAL JSValue minnet_asynciterator_proto, minnet_asynciterator_ctor;

enum {
  ASYNCITERATOR_NEXT,
  ASYNCITERATOR_PUSH,
  ASYNCITERATOR_STOP,
};

static JSValue
minnet_asynciterator_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  AsyncIterator* iter;

  if(!(iter = minnet_asynciterator_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case ASYNCITERATOR_NEXT: {
      ret = asynciterator_next(iter, argc > 0 ? argv[0] : JS_UNDEFINED, ctx);
      break;
    }
    case ASYNCITERATOR_PUSH: {
      ret = JS_NewBool(ctx, asynciterator_yield(iter, argv[0], ctx));
      break;
    }
    case ASYNCITERATOR_STOP: {
      ret = JS_NewBool(ctx, asynciterator_stop(iter, argc > 0 ? argv[0] : JS_UNDEFINED, ctx));
      break;
    }
  }

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

static const JSClassDef minnet_asynciterator_class = {
    "AsyncIterator",
    .finalizer = minnet_asynciterator_finalizer,
};

static const JSCFunctionListEntry minnet_asynciterator_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("next", 0, minnet_asynciterator_method, ASYNCITERATOR_NEXT),
    JS_CFUNC_MAGIC_DEF("push", 1, minnet_asynciterator_method, ASYNCITERATOR_PUSH),
    JS_CFUNC_MAGIC_DEF("stop", 0, minnet_asynciterator_method, ASYNCITERATOR_STOP),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AsyncIterator", JS_PROP_CONFIGURABLE),
};

int
minnet_asynciterator_init(JSContext* ctx, JSModuleDef* m) {
  // Add class AsyncIterator
  JS_NewClassID(&minnet_asynciterator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), minnet_asynciterator_class_id, &minnet_asynciterator_class);

  JSValue asynciterator_proto = js_asyncgenerator_prototype(ctx);
  minnet_asynciterator_proto = JS_NewObjectProto(ctx, asynciterator_proto);
  JS_FreeValue(ctx, asynciterator_proto);
  JS_SetPropertyFunctionList(ctx, minnet_asynciterator_proto, minnet_asynciterator_proto_funcs, countof(minnet_asynciterator_proto_funcs));
  JS_SetClassProto(ctx, minnet_asynciterator_class_id, minnet_asynciterator_proto);

  minnet_asynciterator_ctor = JS_NewCFunction2(ctx, minnet_asynciterator_constructor, "MinnetAsyncIterator", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, minnet_asynciterator_ctor, minnet_asynciterator_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "AsyncIterator", minnet_asynciterator_ctor);

  return 0;
}

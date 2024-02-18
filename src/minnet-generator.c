#include "minnet-generator.h"
#include "js-utils.h"
#include <quickjs.h>
#include <assert.h>
#include <libwebsockets.h>

THREAD_LOCAL JSClassID minnet_generator_class_id;
THREAD_LOCAL JSValue minnet_generator_proto, minnet_generator_ctor;

enum {
  GENERATOR_NEXT = 0,
  GENERATOR_RETURN,
  GENERATOR_THROW,
  GENERATOR_WRITE,
  GENERATOR_ENQUEUE,
  GENERATOR_CONTINUOUS,
  GENERATOR_BUFFERING,
  GENERATOR_STOP,
  GENERATOR_ITERATOR,
};

static JSValue
minnet_generator_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  MinnetGenerator* gen = (MinnetGenerator*)opaque;
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case GENERATOR_NEXT: {
      ret = generator_next(gen, argc > 0 ? argv[0] : JS_UNDEFINED);
      break;
    }

    case GENERATOR_RETURN: {
      ResolveFunctions async = {JS_NULL, JS_NULL};
      ret = js_async_create(ctx, &async);
      asynciterator_stop(&gen->iterator, argc > 0 ? argv[0] : JS_UNDEFINED, ctx);
      JSValue result = js_iterator_result(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, TRUE);
      js_async_resolve(ctx, &async, result);
      JS_FreeValue(ctx, result);
      break;
    }

    case GENERATOR_THROW: {
      /*  ResolveFunctions async = {JS_NULL, JS_NULL};
        ret = js_async_create(ctx, &async);
        asynciterator_cancel(&gen->iterator, argv[0], ctx);
        js_async_reject(ctx, &async, argv[0]);*/
      ret = generator_throw(gen, argv[0]);
      break;
    }
  }
  return ret;
}

static JSValue
minnet_generator_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MinnetGenerator* gen;
  JSValue ret = JS_UNDEFINED;

  if(!(gen = minnet_generator_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case GENERATOR_WRITE: {
      JSBuffer buf = js_input_chars(ctx, argv[0]);

      ssize_t result = generator_write(gen, buf.data, buf.size, argc > 1 ? argv[1] : JS_NULL);
      ret = JS_NewInt64(ctx, result);

      break;
    }

    case GENERATOR_ENQUEUE: {
      ssize_t result = generator_enqueue(gen, argv[0]);
      ret = JS_NewInt64(ctx, result);
      break;
    }

    case GENERATOR_CONTINUOUS: {
      ret = JS_NewBool(ctx, generator_continuous(gen, argc > 0 ? argv[0] : JS_NULL));
      break;
    }

    case GENERATOR_BUFFERING: {
      size_t sz = 4096;
      if(argc > 0)
        JS_ToIndex(ctx, &sz, argv[0]);

      ret = JS_NewBool(ctx, generator_buffering(gen, sz));
      break;
    }

    case GENERATOR_STOP: {
      ret = JS_NewBool(ctx, generator_stop(gen, argc > 0 ? argv[0] : JS_NULL));
      break;
    }

    case GENERATOR_ITERATOR: {
      ret = minnet_generator_iterator(ctx, gen);
      break;
    }
  }

  return ret;
}

static JSValue
minnet_generator_push(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  MinnetGenerator* gen = (MinnetGenerator*)opaque;
  JSValue ret = JS_UNDEFINED;

  if(argc < 1)
    return JS_ThrowInternalError(ctx, "argument required");

  // JSValue callback = argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_NULL;

  ret = generator_push(gen, argv[0]);
  // JS_FreeValue(ctx, callback);
  return ret;
}

static JSValue
minnet_generator_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  MinnetGenerator* gen = (MinnetGenerator*)opaque;
  JSValue ret = JS_UNDEFINED;

  // JSValue callback = argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_NULL;

  ret = JS_NewBool(ctx, generator_stop(gen, JS_NULL));

  // JS_FreeValue(ctx, callback);

  return ret;
}

JSValue
minnet_generator_wrap(JSContext* ctx, MinnetGenerator* req) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_generator_proto, minnet_generator_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, generator_dup(req));

  return ret;
}

JSValue
minnet_generator_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  MinnetGenerator* gen;
  JSValue args[2];

  if(!(gen = generator_new(ctx)))
    return JS_EXCEPTION;

  if(argc < 1 || !JS_IsFunction(ctx, argv[0]))
    return JS_ThrowInternalError(ctx, "MinnetGenerator needs a function parameter");

  args[0] = js_function_cclosure(ctx, minnet_generator_push, 0, 0, generator_dup(gen), (void (*)(void*)) & generator_free);
  args[1] = js_function_cclosure(ctx, minnet_generator_stop, 0, 0, generator_dup(gen), (void (*)(void*)) & generator_free);

  gen->executor = js_function_bind_argv(ctx, argv[0], 2, args);
  /*  ret = JS_Call(ctx, argv[0], JS_UNDEFINED, 2, args);

    JS_FreeValue(ctx, ret);*/

  if(argc > 1) {
    size_t sz = 4096;
    JS_ToIndex(ctx, &sz, argv[1]);
    generator_buffering(gen, sz);
  }

  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);

  return minnet_generator_wrap(ctx, gen);
  // return minnet_generator_iterator(ctx, gen);
}

static const JSCFunctionListEntry minnet_generator_iter[] = {
    JS_CFUNC_DEF("[Symbol.asyncIterator]", 0, (JSCFunction*)&JS_DupValue),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetGeneratorIterator", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry minnet_generator_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("write", 1, minnet_generator_method, GENERATOR_WRITE),
    JS_CFUNC_MAGIC_DEF("enqueue", 1, minnet_generator_method, GENERATOR_ENQUEUE),
    JS_CFUNC_MAGIC_DEF("continuous", 0, minnet_generator_method, GENERATOR_CONTINUOUS),
    JS_CFUNC_MAGIC_DEF("buffering", 0, minnet_generator_method, GENERATOR_BUFFERING),
    JS_CFUNC_MAGIC_DEF("stop", 0, minnet_generator_method, GENERATOR_STOP),
    JS_CFUNC_MAGIC_DEF("[Symbol.asyncIterator]", 0, minnet_generator_method, GENERATOR_ITERATOR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetGenerator", JS_PROP_CONFIGURABLE),
};

JSValue
minnet_generator_iterator(JSContext* ctx, MinnetGenerator* gen) {
  static const char* method_names[] = {
      "next",
      "return",
      "throw",
  };
  JSValue ret, proto = js_asyncgenerator_prototype(ctx);
  ret = JS_NewObjectProto(ctx, proto);
  JS_FreeValue(ctx, proto);

  for(size_t i = 0; i < countof(method_names); i++) {
    JSValue func = js_function_cclosure(ctx, minnet_generator_function, 0, i, generator_dup(gen), (void*)&generator_free);
    JS_DefinePropertyValueStr(ctx, ret, method_names[i], func, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
  }

  JS_SetPropertyFunctionList(ctx, ret, minnet_generator_iter, countof(minnet_generator_iter));

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

static void
minnet_generator_finalizer(JSRuntime* rt, JSValue val) {
  MinnetGenerator* g;

  if((g = JS_GetOpaque(val, minnet_generator_class_id))) {
    generator_free(g);
  }
}

static const JSClassDef minnet_generator_class = {
    "MinnetGenerator",
    .finalizer = minnet_generator_finalizer,
};

int
minnet_generator_init(JSContext* ctx, JSModuleDef* m) {

  // Add class Generator
  JS_NewClassID(&minnet_generator_class_id);

  JS_NewClass(JS_GetRuntime(ctx), minnet_generator_class_id, &minnet_generator_class);
  minnet_generator_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_generator_proto, minnet_generator_proto_funcs, countof(minnet_generator_proto_funcs));
  JS_SetClassProto(ctx, minnet_generator_class_id, minnet_generator_proto);

  minnet_generator_ctor = JS_NewCFunction2(ctx, minnet_generator_constructor, "MinnetGenerator", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, minnet_generator_ctor, minnet_generator_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Generator", minnet_generator_ctor);

  return 0;
}

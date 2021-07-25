#include "jsutils.h"

JSValue
vector2array(JSContext* ctx, int argc, JSValue argv[]) {
  int i;
  JSValue ret = JS_NewArray(ctx);
  for(i = 0; i < argc; i++) JS_SetPropertyUint32(ctx, ret, i, argv[i]);
  return ret;
}

void
js_console_log(JSContext* ctx, JSValue* console, JSValue* console_log) {
  JSValue global = JS_GetGlobalObject(ctx);
  *console = JS_GetPropertyStr(ctx, global, "console");
  *console_log = JS_GetPropertyStr(ctx, *console, "log");
  JS_FreeValue(ctx, global);
}

JSValue
js_function_bound(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue* func_data) {
  JSValue args[argc + magic];
  int i, j;
  for(i = 0; i < magic; i++) args[i] = func_data[i + 1];
  for(j = 0; j < argc; j++) args[i++] = argv[j];

  return JS_Call(ctx, func_data[0], this_val, i, args);
}

JSValue
js_function_bind(JSContext* ctx, JSValueConst func, int argc, JSValueConst argv[]) {
  JSValue data[argc + 1];
  int i;
  data[0] = JS_DupValue(ctx, func);
  for(i = 0; i < argc; i++) data[i + 1] = JS_DupValue(ctx, argv[i]);
  return JS_NewCFunctionData(ctx, js_function_bound, 0, argc, argc + 1, data);
}

JSValue
js_function_bind_1(JSContext* ctx, JSValueConst func, JSValueConst arg) {
  return js_function_bind(ctx, func, 1, &arg);
}

/*JSValue
js_iterator_method(JSContext* ctx, JSValueConst obj) {
  JSAtom atom;
  JSValue ret = JS_UNDEFINED;
  atom = js_symbol_static_atom(ctx, "iterator");
  if(JS_HasProperty(ctx, obj, atom))
    ret = JS_GetProperty(ctx, obj, atom);

  JS_FreeAtom(ctx, atom);
  if(!JS_IsFunction(ctx, ret)) {
    atom = js_symbol_static_atom(ctx, "asyncIterator");
    if(JS_HasProperty(ctx, obj, atom))
      ret = JS_GetProperty(ctx, obj, atom);

    JS_FreeAtom(ctx, atom);
  }
  return ret;
}

JSValue
js_iterator_new(JSContext* ctx, JSValueConst obj) {
  JSValue fn, ret;
  fn = js_iterator_method(ctx, obj);
  ret = JS_Call(ctx, fn, obj, 0, 0);
  JS_FreeValue(ctx, fn);
  return ret;
}*/

BOOL
js_is_iterator(JSContext* ctx, JSValueConst obj) {
  if(JS_IsObject(obj)) {
    JSValue next = JS_GetPropertyStr(ctx, obj, "next");
    if(JS_IsFunction(ctx, next))
      return TRUE;
  }
  return FALSE;
}

JSValue
js_iterator_next(JSContext* ctx, JSValueConst obj, BOOL* done_p) {
  JSValue fn, result, done, value;
  if(!JS_IsObject(obj))
    return JS_ThrowTypeError(ctx, "argument is not an object");

  fn = JS_GetPropertyStr(ctx, obj, "next");
  if(!JS_IsFunction(ctx, fn))
    return JS_ThrowTypeError(ctx, "object does not have 'next' method");

  result = JS_Call(ctx, fn, obj, 0, 0);
  JS_FreeValue(ctx, fn);

  if(JS_IsException(result))
    return JS_EXCEPTION;

  done = JS_GetPropertyStr(ctx, result, "done");
  value = JS_GetPropertyStr(ctx, result, "value");
  JS_FreeValue(ctx, result);
  *done_p = JS_ToBool(ctx, done);
  JS_FreeValue(ctx, done);
  return value;
}
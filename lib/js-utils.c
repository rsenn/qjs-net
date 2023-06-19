/**
 * @file js-utils.c
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "js-utils.h"
#include "buffer.h"

JSValue
js_object_constructor(JSContext* ctx, JSValueConst value) {
  JSValue ctor = JS_UNDEFINED;
  if(JS_IsObject(value))
    ctor = JS_GetPropertyStr(ctx, value, "constructor");
  return ctor;
}

char*
js_object_classname(JSContext* ctx, JSValueConst value) {
  JSValue proto = JS_UNDEFINED, ctor;
  const char* name;
  char* s = 0;
  ctor = js_object_constructor(ctx, value);
  if(!JS_IsFunction(ctx, ctor)) {
    proto = JS_GetPrototype(ctx, value);
    ctor = js_object_constructor(ctx, proto);
  }
  if((name = js_function_name(ctx, ctor))) {
    s = name && name[0] ? js_strdup(ctx, name) : 0;
    JS_FreeCString(ctx, name);
  }

  JS_FreeValue(ctx, ctor);
  JS_FreeValue(ctx, proto);
  return s;
}

const char*
js_object_tostring(JSContext* ctx, JSValueConst value) {
  JSValue method = js_global_prototype_func(ctx, "Object", "toString");
  const char* str = js_object_tostring2(ctx, method, value);
  JS_FreeValue(ctx, method);
  return str;
}

const char*
js_object_tostring2(JSContext* ctx, JSValueConst method, JSValueConst value) {
  JSValue str = JS_Call(ctx, method, value, 0, 0);
  const char* s = JS_ToCString(ctx, str);
  JS_FreeValue(ctx, str);
  return s;
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
  int i, j = 0, k = 0, l, count = magic & JS_BOUND_MASK;
  JSValue args[argc + count], return_val, ret = JS_UNDEFINED;

  l = JS_NUM_FUNCTIONS(magic);
  i = l;

  if(magic & JS_RETURN_MASK) {
    int index = ((magic & JS_RETURN_MASK) >> JS_RETURN_SHIFT) - 1;
    return_val = index >= argc ? JS_UNDEFINED : argv[index];
  } else if(magic & JS_BIND_RETVAL) {
    return_val = func_data[i++];
  }

  if(magic & JS_BIND_THIS)
    this_val = func_data[i++];

  for(j = 0; j < count; j++)
    args[k++] = func_data[i + j];

  for(j = 0; j < argc; j++)
    args[k++] = argv[j];

  for(i = 0; i < l; i++) {
    JS_FreeValue(ctx, ret);
    ret = JS_Call(ctx, func_data[i], this_val, k, args);
  }

  if(magic & JS_RETURN_MASK) {
    JS_FreeValue(ctx, ret);
    ret = JS_DupValue(ctx, return_val);
  }

  return ret;
}

JSValue
js_function_bind_argv(JSContext* ctx, JSValueConst func, int flags, JSValueConst argv[]) {
  int i, argc = flags & JS_BOUND_MASK;
  JSValue data[argc + 1];

  data[0] = JS_DupValue(ctx, func);
  for(i = 0; i < argc; i++)
    data[i + 1] = JS_DupValue(ctx, argv[i]);

  return JS_NewCFunctionData(ctx, js_function_bound, 0, flags, argc + 1, data);
}

JSValue
js_function_bind_functions(JSContext* ctx, int num, ...) {
  int i;
  JSValue data[num];
  va_list list;

  va_start(list, num);

  for(i = 0; i < num; i++) {
    data[i] = JS_DupValue(ctx, va_arg(list, JSValueConst));
  }

  va_end(list);
  return JS_NewCFunctionData(ctx, js_function_bound, 0, JS_BIND_MAGIC(num, 0), i, data);
}

JSValue
js_function_bind_1(JSContext* ctx, JSValueConst func, JSValueConst arg) {
  return js_function_bind_argv(ctx, func, 1, &arg);
}

JSValue
js_function_bind_this(JSContext* ctx, JSValueConst func, JSValueConst this_val) {
  return js_function_bind_argv(ctx, func, 1 | JS_BIND_THIS, &this_val);
}

JSValue
js_function_bind_this_1(JSContext* ctx, JSValueConst func, JSValueConst this_val, JSValueConst arg) {
  JSValueConst bound[] = {this_val, arg};
  return js_function_bind_argv(ctx, func, countof(bound) | JS_BIND_THIS, bound);
}

JSValue
js_function_bind_return(JSContext* ctx, JSValueConst func, int argument) {
  return js_function_bind_argv(ctx, func, JS_BIND_RETURN(argument), 0);
}

JSValue
js_function_name_value(JSContext* ctx, JSValueConst value) {
  JSValue str, name, args[2], idx;
  int32_t i = -1;
  str = js_invoke(ctx, value, "toString", 0, 0);
  args[0] = JS_NewString(ctx, "function");
  idx = js_invoke(ctx, str, "indexOf", 1, args);
  JS_FreeValue(ctx, args[0]);
  JS_ToInt32(ctx, &i, idx);
  if(i != 0 && i != 6) {
    JS_FreeValue(ctx, str);
    return JS_UNDEFINED;
  }
  args[0] = JS_NewString(ctx, " ");
  args[1] = JS_NewUint32(ctx, i + 1);
  idx = js_invoke(ctx, str, "indexOf", 2, args);
  JS_ToInt32(ctx, &i, idx);

  args[0] = JS_NewString(ctx, "(");
  idx = js_invoke(ctx, str, "indexOf", 1, args);
  JS_FreeValue(ctx, args[0]);
  args[0] = JS_NewUint32(ctx, i + 1);
  args[1] = idx;
  name = js_invoke(ctx, str, "substring", 2, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, str);
  return name;
}

const char*
js_function_name(JSContext* ctx, JSValueConst value) {
  JSValue name = js_function_name_value(ctx, value);
  const char* str = js_is_nullish(name) ? 0 : JS_ToCString(ctx, name);
  JS_FreeValue(ctx, name);
  return str;
}

JSValue
js_function_prototype(JSContext* ctx) {
  JSValue ret, fn = JS_NewCFunction(ctx, 0, "", 0);
  ret = JS_GetPrototype(ctx, fn);
  JS_FreeValue(ctx, fn);
  return ret;
}

JSAtom
js_iterable_method(JSContext* ctx, JSValueConst obj, BOOL* async_ptr) {
  JSAtom atom;
  atom = js_symbol_static_atom(ctx, "asyncIterator");
  if(JS_HasProperty(ctx, obj, atom)) {
    if(async_ptr)
      *async_ptr = TRUE;
    return atom;
  }

  JS_FreeAtom(ctx, atom);

  atom = js_symbol_static_atom(ctx, "iterator");
  if(JS_HasProperty(ctx, obj, atom)) {
    if(async_ptr)
      *async_ptr = FALSE;
    return atom;
  }

  JS_FreeAtom(ctx, atom);
  return 0;
}

JSValue
js_iterator_result(JSContext* ctx, JSValueConst value, BOOL done) {
  JSValue ret = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, ret, "value", JS_DupValue(ctx, value));
  JS_SetPropertyStr(ctx, ret, "done", JS_NewBool(ctx, done));

  return ret;
}

JSValue
js_iterator_next(JSContext* ctx, JSValueConst obj, JSValue* next, BOOL* done_p, int argc, JSValueConst argv[]) {
  JSValue fn, result, done, value;

  if(!JS_IsObject(obj))
    return JS_ThrowTypeError(ctx, "argument is not an object (%d) \"%s\"", JS_VALUE_GET_TAG(obj), JS_ToCString(ctx, obj));

  if(JS_IsObject(*next) && JS_IsFunction(ctx, *next)) {
    fn = *next;
  } else {
    if(JS_IsFunction(ctx, obj)) {
      JSValue tmp = JS_Call(ctx, obj, JS_UNDEFINED, 0, 0);
      if(JS_IsObject(tmp)) {
        JS_FreeValue(ctx, obj);
        obj = tmp;
      }
    }

    fn = JS_GetPropertyStr(ctx, obj, "next");

    if(JS_IsUndefined(fn))
      return JS_ThrowTypeError(ctx, "object does not have 'next' method");

    if(!JS_IsFunction(ctx, fn))
      return JS_ThrowTypeError(ctx, "object.next is not a function");

    *next = js_function_bind_this(ctx, fn, obj);
    JS_FreeValue(ctx, fn);
    fn = *next;
  }

  result = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);
  // JS_FreeValue(ctx, fn);

  if(JS_IsException(result))
    return JS_EXCEPTION;

  if(js_is_promise(ctx, result))
    return result;

  done = JS_GetPropertyStr(ctx, result, "done");
  value = JS_GetPropertyStr(ctx, result, "value");
  *done_p = JS_ToBool(ctx, done);
  JS_FreeValue(ctx, result);
  JS_FreeValue(ctx, done);
  return value;
}

int
js_copy_properties(JSContext* ctx, JSValueConst dst, JSValueConst src, int flags) {
  JSPropertyEnum* tab;
  uint32_t tab_len, i;

  if(JS_GetOwnPropertyNames(ctx, &tab, &tab_len, src, flags))
    return -1;

  for(i = 0; i < tab_len; i++) {
    JSValue value = JS_GetProperty(ctx, src, tab[i].atom);
    JS_SetProperty(ctx, dst, tab[i].atom, value);
  }
  return i;
}

void
js_buffer_free_default(JSRuntime* rt, void* opaque, void* ptr) {
  JSBuffer* buf = opaque;

  if(JS_IsString(buf->value))
    JS_FreeValueRT(rt, buf->value);
  else if(!JS_IsUndefined(buf->value))
    JS_FreeValueRT(rt, buf->value);

  buf->value = JS_UNDEFINED;
  buf->data = 0;
  buf->size = 0;
  buf->pos = 0;
  ol_init(&buf->range);
}

BOOL
js_buffer_from(JSContext* ctx, JSBuffer* buf, JSValueConst value) {
  *buf = js_input_chars(ctx, value);
  return !!buf->data;
}

JSBuffer
js_buffer_new(JSContext* ctx, JSValueConst value) {
  JSBuffer ret = {0, 0, 0, &js_buffer_free_default, JS_UNDEFINED, {0, -1}};
  ret.free = &js_buffer_free_default;

  if(JS_IsString(value)) {
    ret.data = (uint8_t*)JS_ToCStringLen(ctx, &ret.size, value);
    ret.value = value;
  } else if((ret.data = JS_GetArrayBuffer(ctx, &ret.size, value))) {
    ret.value = JS_DupValue(ctx, value);
  }

  return ret;
}

JSBuffer
js_buffer_fromblock(JSContext* ctx, struct byte_block* blk) {
  JSValue buf = block_toarraybuffer(blk, ctx);

  return js_buffer_new(ctx, buf);
}

JSBuffer
js_buffer_alloc(JSContext* ctx, size_t size) {
  ByteBlock block = {0, 0};

  if((block.start = js_malloc(ctx, size)))
    block.end = block.start + size;

  return js_buffer_fromblock(ctx, &block);
}

void
js_buffer_free_rt(JSBuffer* in, JSRuntime* rt) {
  if(in->data) {
    in->free(rt, in, in->data);
    in->data = 0;
    in->size = 0;
    in->value = JS_UNDEFINED;
  }
}

void
js_buffer_free(JSBuffer* in, JSContext* ctx) {
  js_buffer_free_rt(in, JS_GetRuntime(ctx));
}

BOOL
js_function_is_async(JSContext* ctx, JSValueConst obj) {
  BOOL ret = FALSE;
  const char* str;

  if(!JS_IsFunction(ctx, obj))
    return FALSE;

  str = JS_ToCString(ctx, obj);
  ret = !strncmp(str, "async", 5);
  JS_FreeCString(ctx, str);
  return ret;
}

BOOL
js_function_is_generator(JSContext* ctx, JSValueConst obj, BOOL* async_ptr) {
  BOOL ret = FALSE, async = FALSE;
  const char *str, *x;

  if(!JS_IsFunction(ctx, obj))
    return FALSE;

  x = str = JS_ToCString(ctx, obj);

  if(!strncmp(x, "async ", 6)) {
    x += 6;
    if(async)
      async = TRUE;
  }

  if(!strncmp(x, "function", 8)) {
    x += 8;
    while(isspace(*x))
      ++x;
  }

  if(*x == '*')
    ret = TRUE;

  if(ret && async_ptr)
    *async_ptr = async;

  JS_FreeCString(ctx, str);
  return ret;
}

BOOL
js_is_iterator(JSContext* ctx, JSValueConst obj) {
  if(JS_IsObject(obj)) {
    JSValue next = JS_GetPropertyStr(ctx, obj, "next");

    if(JS_IsFunction(ctx, next))
      return TRUE;
  }
  return FALSE;
}

BOOL
js_is_generator(JSContext* ctx, JSValueConst obj) {
  BOOL ret = FALSE;
  const char* str;

  if((str = js_object_tostring(ctx, obj))) {
    ret = !!strstr(str, "Generator");
    JS_FreeCString(ctx, str);
  }

  return ret;
}

BOOL
js_is_async_generator(JSContext* ctx, JSValueConst obj) {
  BOOL ret = FALSE;
  const char* str;

  if((str = js_object_tostring(ctx, obj))) {
    ret = !!strstr(str, "AsyncGenerator");
    JS_FreeCString(ctx, str);
  }
  return ret;
}

JSAtom
js_symbol_static_atom(JSContext* ctx, const char* name) {
  JSValue sym = js_symbol_static_value(ctx, name);
  JSAtom ret = JS_IsUndefined(sym) ? -1 : JS_ValueToAtom(ctx, sym);
  JS_FreeValue(ctx, sym);
  return ret;
}

JSValue
js_symbol_static_value(JSContext* ctx, const char* name) {
  JSValue symbol_ctor, ret;
  symbol_ctor = js_symbol_ctor(ctx);
  ret = JS_GetPropertyStr(ctx, symbol_ctor, name);
  JS_FreeValue(ctx, symbol_ctor);
  return ret;
}

JSValue
js_symbol_for_value(JSContext* ctx, const char* name) {
  JSValue symbol_ctor, nameval, ret;
  JSAtom for_atom = JS_NewAtom(ctx, "for");
  symbol_ctor = js_symbol_ctor(ctx);
  nameval = JS_NewString(ctx, name);
  ret = JS_Invoke(ctx, symbol_ctor, for_atom, 1, &nameval);
  JS_FreeAtom(ctx, for_atom);
  JS_FreeValue(ctx, nameval);
  JS_FreeValue(ctx, symbol_ctor);
  return ret;
}

JSAtom
js_symbol_for_atom(JSContext* ctx, const char* name) {
  JSValue sym = js_symbol_for_value(ctx, name);
  JSAtom ret = JS_IsUndefined(sym) ? -1 : JS_ValueToAtom(ctx, sym);
  JS_FreeValue(ctx, sym);
  return ret;
}
JSValue
js_symbol_ctor(JSContext* ctx) {
  return js_global_get(ctx, "Symbol");
}

JSValue
js_global_get(JSContext* ctx, const char* prop) {
  JSValue global_obj, ret;
  global_obj = JS_GetGlobalObject(ctx);
  ret = JS_GetPropertyStr(ctx, global_obj, prop);
  JS_FreeValue(ctx, global_obj);
  return ret;
}

JSValue
js_global_os(JSContext* ctx) {
  return js_global_get(ctx, "os");
}

JSValue
js_global_prototype(JSContext* ctx, const char* class_name) {
  JSValue ctor, ret;
  ctor = js_global_get(ctx, class_name);
  ret = JS_GetPropertyStr(ctx, ctor, "prototype");
  JS_FreeValue(ctx, ctor);
  return ret;
}

JSValue
js_global_prototype_func(JSContext* ctx, const char* class_name, const char* func_name) {
  JSValue proto, func;
  proto = js_global_prototype(ctx, class_name);
  func = JS_GetPropertyStr(ctx, proto, func_name);
  JS_FreeValue(ctx, proto);
  return func;
}

JSValue
js_global_static_func(JSContext* ctx, const char* class_name, const char* func_name) {
  JSValue ctor, func;
  ctor = js_global_get(ctx, class_name);
  func = JS_GetPropertyStr(ctx, ctor, func_name);
  JS_FreeValue(ctx, ctor);
  return func;
}

JSValue
js_os_get(JSContext* ctx, const char* prop) {
  JSValue os_obj = js_global_os(ctx);
  JSValue ret = JS_GetPropertyStr(ctx, os_obj, prop);
  JS_FreeValue(ctx, os_obj);
  return ret;
}

JSValue
js_timer_start(JSContext* ctx, JSValueConst fn, uint32_t ms) {
  JSValue set_timeout = js_os_get(ctx, "setTimeout");
  JSValueConst args[2] = {fn, JS_MKVAL(JS_TAG_INT, ms)};
  JSValue ret = JS_Call(ctx, set_timeout, JS_UNDEFINED, 2, args);
  JS_FreeValue(ctx, set_timeout);
  return ret;
}

void
js_timer_cancel(JSContext* ctx, JSValueConst timer) {
  JSValue clear_timeout = js_os_get(ctx, "clearTimeout");
  JSValue ret = JS_Call(ctx, clear_timeout, JS_UNDEFINED, 1, &timer);
  JS_FreeValue(ctx, clear_timeout);
  JS_FreeValue(ctx, ret);
}

void
js_timer_free(void* ptr) {
  struct TimerClosure* closure = ptr;
  JSContext* ctx = closure->ctx;

  if(--closure->ref_count == 0) {
    JS_FreeValue(ctx, closure->id);
    JS_FreeValue(ctx, closure->handler);
    JS_FreeValue(ctx, closure->callback);

    js_free(ctx, closure);
  }
}

JSValue
js_timer_callback(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  struct TimerClosure* closure = opaque;
  JSValue ret;
  JSValueConst args[] = {closure->id, closure->handler, closure->callback, JS_NewUint32(ctx, closure->interval)};

  ret = JS_Call(ctx, closure->handler, this_val, countof(args), args);

  if(!(JS_IsBool(ret) && !JS_ToBool(ctx, ret))) {
    JS_FreeValue(ctx, closure->id);
    closure->id = js_timer_start(ctx, closure->callback, closure->interval);
  }

  return ret;
}

struct TimerClosure*
js_timer_interval(JSContext* ctx, JSValueConst fn, uint32_t ms) {
  struct TimerClosure* closure;

  if(!(closure = js_malloc(ctx, sizeof(struct TimerClosure))))
    return 0;

  closure->ref_count = 1;
  closure->ctx = ctx;
  closure->interval = ms;
  closure->handler = JS_DupValue(ctx, fn);
  closure->callback = js_function_cclosure(ctx, js_timer_callback, 0, 0, closure, js_timer_free);
  closure->id = js_timer_start(ctx, closure->callback, ms);

  return closure;
}

void
js_timer_restart(struct TimerClosure* closure) {
  js_timer_cancel(closure->ctx, closure->id);
  JS_FreeValue(closure->ctx, closure->id);
  closure->id = js_timer_start(closure->ctx, closure->callback, closure->interval);
}

char*
js_tostringlen(JSContext* ctx, size_t* lenp, JSValueConst value) {
  size_t len;
  const char* cstr;
  char* ret = 0;
  if((cstr = JS_ToCStringLen(ctx, &len, value))) {
    ret = js_strndup(ctx, cstr, len);
    if(lenp)
      *lenp = len;
    JS_FreeCString(ctx, cstr);
  }
  return ret;
}

char*
js_tostring(JSContext* ctx, JSValueConst value) {
  return js_tostringlen(ctx, 0, value);
}

JSValue
js_invoke(JSContext* ctx, JSValueConst this_obj, const char* method, int argc, JSValueConst argv[]) {
  JSAtom atom = JS_NewAtom(ctx, method);
  JSValue ret = JS_Invoke(ctx, this_obj, atom, argc, argv);
  JS_FreeAtom(ctx, atom);
  return ret;
}

BOOL
js_is_promise(JSContext* ctx, JSValueConst value) {
  BOOL ret = FALSE;

  if(JS_IsObject(value)) {
    JSValue ctor;

    ctor = js_global_get(ctx, "Promise");
    ret = JS_IsInstanceOf(ctx, value, ctor);

    JS_FreeValue(ctx, ctor);
  }

  return ret;
}

JSValue
js_error_new(JSContext* ctx, const char* fmt, ...) {
  va_list args;
  JSValue err = JS_NewError(ctx);
  char buf[1024];

  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, buf));
  return err;
}

void
js_error_print(JSContext* ctx, JSValueConst error) {
  const char *str = 0, *stack = 0;

  if(JS_IsObject(error)) {
    JSValue st = JS_GetPropertyStr(ctx, error, "stack");

    if(!JS_IsUndefined(st))
      stack = JS_ToCString(ctx, st);

    JS_FreeValue(ctx, st);
  }

  // lwsl_err("Toplevel error:");

  if(!JS_IsNull(error) && (str = JS_ToCString(ctx, error))) {
    const char* type = js_object_classname(ctx, error);
    const char* exception = str;
    size_t typelen = strlen(type);

    if(!strncmp(exception, type, typelen) && exception[typelen] == ':') {
      exception += typelen + 2;
    }

    lwsl_err("Exception %s: %s", type, exception);
    /*printf("Exception %s: %s\n", type, exception);*/
  }

  if(stack && *stack) {
    size_t pos = 0, i = 0, len, end = strlen(stack);
    lwsl_err("Stack:");
    /*printf("Stack:\n");*/

    while(i < end) {
      len = byte_chrs(&stack[i], end - i, "\r\n", 2);

      lwsl_err("%zu: %.*s", pos++, (int)len, &stack[i]);
      /*printf("%zu: %.*s\n", pos++, (int)len, &stack[i]);*/
      i += len;

      i += scan_charsetnskip(&stack[i], "\r\n", end - i);
    }
  }
  if(stack)
    JS_FreeCString(ctx, stack);
  if(str)
    JS_FreeCString(ctx, str);
}

char*
js_error_string(JSContext* ctx, JSValueConst error) {
  DynBuf buf;
  const char *str = 0, *stack = 0;

  dbuf_init2(&buf, ctx, (DynBufReallocFunc*)js_realloc);

  if(JS_IsObject(error)) {
    JSValue st = JS_GetPropertyStr(ctx, error, "stack");

    if(!JS_IsUndefined(st))
      stack = JS_ToCString(ctx, st);

    JS_FreeValue(ctx, st);
  }

  if(!JS_IsNull(error) && (str = JS_ToCString(ctx, error))) {
    const char* type = js_object_classname(ctx, error);
    const char* exception = str;
    size_t typelen = strlen(type);

    if(!strncmp(exception, type, typelen) && exception[typelen] == ':') {
      exception += typelen + 2;
    }

    dbuf_printf(&buf, "Exception %s: %s", type, exception);
  }

  if(stack && *stack) {
    size_t pos = 0, i = 0, len, end = strlen(stack);
    dbuf_printf(&buf, "\nStack:\n%s", stack);
  }

  dbuf_put(&buf, "\0", 1);

  return buf.buf;
}

uint8_t*
js_toptrsize(JSContext* ctx, unsigned int* plen, JSValueConst value) {
  size_t n = 0;
  void *ret = 0, *ptr;
  if((ptr = JS_GetArrayBuffer(ctx, &n, value))) {
    if((ret = js_malloc(ctx, n)))
      memcpy(ret, ptr, n);
  }
  return ret;
}

uint32_t
js_get_propertystr_uint32(JSContext* ctx, JSValueConst obj, const char* str) {
  uint32_t ret = 0;
  JSValue value;
  value = JS_GetPropertyStr(ctx, obj, str);
  JS_ToUint32(ctx, &ret, value);
  JS_FreeValue(ctx, value);
  return ret;
}

const char*
js_get_propertystr_cstring(JSContext* ctx, JSValueConst obj, const char* prop) {
  JSAtom atom = JS_NewAtom(ctx, prop);
  const char* ret = 0;

  if(JS_HasProperty(ctx, obj, atom)) {
    JSValue value = JS_GetProperty(ctx, obj, atom);
    ret = JS_ToCString(ctx, value);
    JS_FreeValue(ctx, value);
  }

  return ret;
}

BOOL
js_get_propertystr_bool(JSContext* ctx, JSValueConst obj, const char* str) {
  BOOL ret = FALSE;
  JSValue value;
  value = JS_GetPropertyStr(ctx, obj, str);
  if(!JS_IsException(value))
    ret = JS_ToBool(ctx, value);

  JS_FreeValue(ctx, value);
  return ret;
}

BOOL
js_has_propertystr(JSContext* ctx, JSValueConst obj, const char* str) {
  JSAtom prop = JS_NewAtom(ctx, str);
  BOOL ret = JS_HasProperty(ctx, obj, prop);
  JS_FreeAtom(ctx, prop);
  return ret;
}

int64_t
js_array_length(JSContext* ctx, JSValueConst array) {
  int64_t len = -1;
  /*if(js_is_array(ctx, array) || js_is_typedarray(array)|| js_is_array_like(ctx, array))*/ {
    JSValue length = JS_GetPropertyStr(ctx, array, "length");
    if(JS_IsNumber(length))
      JS_ToInt64(ctx, &len, length);
    JS_FreeValue(ctx, length);
  }
  return len;
}

char**
js_array_to_argv(JSContext* ctx, int* argcp, JSValueConst array) {
  int i, len = js_array_length(ctx, array);
  char** ret = js_mallocz(ctx, sizeof(char*) * (len + 1));
  for(i = 0; i < len; i++) {
    JSValue item = JS_GetPropertyUint32(ctx, array, i);
    ret[i] = js_tostring(ctx, item);
    JS_FreeValue(ctx, item);
  }
  if(argcp)
    *argcp = len;
  return ret;
}

int
js_offset_length(JSContext* ctx, int64_t size, int argc, JSValueConst argv[], OffsetLength* off_len_p) {
  int ret = 0;
  int64_t off = 0, len = size;

  if(argc >= 1 && JS_IsNumber(argv[0]))
    if(!JS_ToInt64(ctx, &off, argv[0]))
      ret = 1;

  if(argc >= 2 && JS_IsNumber(argv[1]))
    if(!JS_ToInt64(ctx, &len, argv[1]))
      ret = 2;

  if(size)
    off = ((off % size) + size) % size;

  if(len >= 0)
    len = MIN(len, size - off);
  else
    len = size - off;

  if(off_len_p) {
    off_len_p->offset = off;
    off_len_p->length = len;
  }
  return ret;
}

JSValue
js_argv_to_array(JSContext* ctx, const char* const* argv) {
  JSValue ret = JS_NewArray(ctx);
  if(argv) {
    size_t i;
    for(i = 0; argv[i]; i++)
      JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, argv[i]));
  }
  return ret;
}

BOOL
js_atom_is_index(JSContext* ctx, int64_t* pval, JSAtom atom) {
  JSValue value;
  BOOL ret = FALSE;
  int64_t index;

  if(atom & (1U << 31)) {
    if(pval)
      *pval = atom & (~(1U << 31));
    return TRUE;
  }

  value = JS_AtomToValue(ctx, atom);

  if(JS_IsNumber(value)) {
    JS_ToInt64(ctx, &index, value);
    ret = TRUE;
  } else if(JS_IsString(value)) {
    const char* s = JS_ToCString(ctx, value);
    if(s[0] == '-' && isdigit(s[s[0] == '-'])) {
      index = atoi(s);
      ret = TRUE;
    }
    JS_FreeCString(ctx, s);
  }

  if(ret == TRUE)
    if(pval)
      *pval = index;

  return ret;
}

BOOL
js_atom_compare_string(JSContext* ctx, JSAtom atom, const char* other) {
  const char* str = JS_AtomToCString(ctx, atom);
  BOOL ret = !strcmp(str, other);
  JS_FreeCString(ctx, str);
  return ret;
}

BOOL
js_atom_is_length(JSContext* ctx, JSAtom atom) {
  return js_atom_compare_string(ctx, atom, "length");
}

BOOL
js_atom_is_symbol(JSContext* ctx, JSAtom atom) {
  JSValue value;
  BOOL ret;
  value = JS_AtomToValue(ctx, atom);
  ret = JS_IsSymbol(value);
  JS_FreeValue(ctx, value);
  return ret;
}

JSBuffer
js_input_buffer(JSContext* ctx, JSValueConst value) {
  JSBuffer ret = JS_BUFFER_VALUE(0, 0, JS_UNDEFINED);

  if(js_is_typedarray(ctx, value)) {
    size_t offset, length;

    ret.value = JS_GetTypedArrayBuffer(ctx, value, &offset, &length, NULL);

    if(!JS_IsException(ret.value)) {
      ret.range.offset = offset;
      ret.range.length = length;
    }
  } else if(js_is_arraybuffer(ctx, value) /* || js_is_sharedarraybuffer(ctx, value)*/) {
    ret.value = JS_DupValue(ctx, value);
  }

  if(!JS_IsUndefined(ret.value)) {
    ret.data = JS_GetArrayBuffer(ctx, &ret.size, ret.value);
  } else {
    JS_FreeValue(ctx, ret.value);
    ret.value = JS_ThrowTypeError(ctx, "Invalid type for input buffer");
  }

  return ret;
}

#undef free

JSBuffer
js_input_chars(JSContext* ctx, JSValueConst value) {
  JSBuffer ret = JS_BUFFER_DEFAULT();
  // int64_t offset = 0, length = INT64_MAX;

  ol_init(&ret.range);

  if(JS_IsString(value)) {
    ret.data = (uint8_t*)JS_ToCStringLen(ctx, &ret.size, value);
    ret.value = JS_DupValue(ctx, value);
    ret.free = &js_buffer_free_default;
  } else {
    ret = js_input_buffer(ctx, value);
  }

  return ret;
}

JSBuffer
js_input_args(JSContext* ctx, int argc, JSValueConst argv[]) {
  JSBuffer input = js_input_chars(ctx, argv[0]);

  if(argc > 1)
    js_offset_length(ctx, input.size, argc - 1, argv + 1, &input.range);

  return input;
}

int
js_buffer_fromargs(JSContext* ctx, int argc, JSValueConst argv[], JSBuffer* buf) {
  int ret = 0;
  *buf = js_input_chars(ctx, argv[0]);

  if(buf->size) {
    ++ret;

    if(argc > 1)
      ret += js_offset_length(ctx, buf->size, argc - 1, argv + 1, &buf->range);
  }

  return ret;
}

BOOL
js_is_arraybuffer(JSContext* ctx, JSValueConst value) {
  if(JS_IsObject(value)) {
    size_t len;
    return JS_GetArrayBuffer(ctx, &len, value) != NULL;
  }
  return FALSE;
}

BOOL
js_is_dataview(JSContext* ctx, JSValueConst value) {
  if(JS_IsObject(value)) {
    JSAtom atoms[] = {
        JS_NewAtom(ctx, "byteLength"),
        JS_NewAtom(ctx, "byteOffset"),
        JS_NewAtom(ctx, "buffer"),
    };
    unsigned i;
    BOOL ret = TRUE;

    for(i = 0; i < countof(atoms); i++) {
      if(!JS_HasProperty(ctx, value, atoms[i])) {
        ret = FALSE;
        break;
      }
    }

    return ret;
  }
  return FALSE;
}

BOOL
js_is_typedarray(JSContext* ctx, JSValueConst value) {
  return js_is_dataview(ctx, value) && js_has_propertystr(ctx, value, "BYTES_PER_ELEMENT");
}

JSValue
js_typedarray_constructor(JSContext* ctx, int bits, BOOL floating, BOOL sign) {
  char class_name[64];

  sprintf(class_name, "%s%s%dArray", (!floating && bits >= 64) ? "Big" : "", floating ? "Float" : sign ? "Int" : "Uint", bits);

  return js_global_get(ctx, class_name);
}

JSValue
js_typedarray_new(JSContext* ctx, int bits, BOOL floating, BOOL sign, JSValueConst buffer, uint32_t byte_offset, uint32_t length) {
  JSValue ctor = js_typedarray_constructor(ctx, bits, floating, sign);
  JSValue args[] = {
      buffer,
      JS_NewUint32(ctx, byte_offset),
      JS_NewUint32(ctx, length),
  };
  JSValue ret = JS_CallConstructor(ctx, ctor, countof(args), args);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, args[2]);
  JS_FreeValue(ctx, ctor);
  return ret;
}

typedef struct {
  CClosureFunc* func;
  uint16_t length, magic;
  void* opaque;
  void (*opaque_finalize)(void*);
} JSCClosureRecord;

static THREAD_LOCAL JSClassID js_cclosure_class_id;

static inline JSCClosureRecord*
js_cclosure_data(JSValueConst value) {
  return JS_GetOpaque(value, js_cclosure_class_id);
}

static inline JSCClosureRecord*
js_cclosure_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_cclosure_class_id);
}

static JSValue
js_cclosure_call(JSContext* ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst argv[], int flags) {
  JSCClosureRecord* ccr;

  if(!(ccr = js_cclosure_data2(ctx, func_obj)))
    return JS_EXCEPTION;

  JSValueConst* arg_buf;
  int i;

  /* XXX: could add the function on the stack for debug */
  if(unlikely(argc < ccr->length)) {
    arg_buf = alloca(sizeof(arg_buf[0]) * ccr->length);
    for(i = 0; i < argc; i++)
      arg_buf[i] = argv[i];
    for(i = argc; i < ccr->length; i++)
      arg_buf[i] = JS_UNDEFINED;
  } else {
    arg_buf = argv;
  }

  return ccr->func(ctx, this_val, argc, arg_buf, ccr->magic, ccr->opaque);
}

static void
js_cclosure_finalizer(JSRuntime* rt, JSValue val) {
  JSCClosureRecord* ccr;

  if((ccr = js_cclosure_data(val))) {
    if(ccr->opaque_finalize)
      ccr->opaque_finalize(ccr->opaque);

    js_free_rt(rt, ccr);
  }
}

static JSClassDef js_cclosure_class = {
    .class_name = "JSCClosure",
    .finalizer = js_cclosure_finalizer,
    .call = js_cclosure_call,
};

JSValue
js_function_cclosure(JSContext* ctx, CClosureFunc* func, int length, int magic, void* opaque, void (*opaque_finalize)(void*)) {
  JSCClosureRecord* ccr;
  JSValue func_proto, func_obj;

  if(js_cclosure_class_id == 0) {
    JS_NewClassID(&js_cclosure_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_cclosure_class_id, &js_cclosure_class);
  }

  func_proto = js_function_prototype(ctx);
  func_obj = JS_NewObjectProtoClass(ctx, func_proto, js_cclosure_class_id);
  JS_FreeValue(ctx, func_proto);

  if(JS_IsException(func_obj))
    return func_obj;

  if(!(ccr = js_malloc(ctx, sizeof(JSCClosureRecord)))) {
    JS_FreeValue(ctx, func_obj);
    return JS_EXCEPTION;
  }

  ccr->func = func;
  ccr->length = length;
  ccr->magic = magic;
  ccr->opaque = opaque;
  ccr->opaque_finalize = opaque_finalize;

  JS_SetOpaque(func_obj, ccr);

  // JS_DefinePropertyValueStr(ctx, func_obj, "length", JS_NewUint32(ctx, length), JS_PROP_CONFIGURABLE);

  return func_obj;
}

JSValue
js_generator_prototype(JSContext* ctx) {
  const char* code = "(function *gen() {})()";
  JSValue ret, gen = JS_Eval(ctx, code, strlen(code), "<internal>", 0);
  ret = JS_GetPrototype(ctx, gen);
  JS_FreeValue(ctx, gen);
  return ret;
}

JSValue
js_asyncgenerator_prototype(JSContext* ctx) {
  const char* code = "(async function *gen() {})()";
  JSValue ret, gen = JS_Eval(ctx, code, strlen(code), "<internal>", 0);
  ret = JS_GetPrototype(ctx, gen);
  JS_FreeValue(ctx, gen);
  return ret;
}

static inline void
js_resolve_functions_zero(ResolveFunctions* funcs) {
  funcs->resolve = JS_NULL;
  funcs->reject = JS_NULL;
}

static inline BOOL
js_resolve_functions_is_null(ResolveFunctions const* funcs) {
  return JS_IsNull(funcs->resolve) && JS_IsNull(funcs->reject);
}

static inline void
js_resolve_functions_call(JSContext* ctx, ResolveFunctions* funcs, int index, JSValueConst arg) {
  JSValue ret = JS_UNDEFINED, *func_ptr = (JSValue*)funcs;
  JSValue func = func_ptr[index];

  assert(!JS_IsNull(func));
  ret = JS_Call(ctx, func, JS_UNDEFINED, 1, &arg);
  js_async_free(ctx, funcs);
  JS_FreeValue(ctx, ret);
}

JSValue
js_async_create(JSContext* ctx, ResolveFunctions* funcs) {
  JSValue ret;

  ret = JS_NewPromiseCapability(ctx, &funcs->resolve);
  return ret;
}

void
js_async_free(JSContext* ctx, ResolveFunctions* funcs) {
  JS_FreeValue(ctx, funcs->resolve);
  JS_FreeValue(ctx, funcs->reject);
  js_resolve_functions_zero(funcs);
}

void
js_async_free_rt(JSRuntime* rt, ResolveFunctions* funcs) {
  JS_FreeValueRT(rt, funcs->resolve);
  JS_FreeValueRT(rt, funcs->reject);
  js_resolve_functions_zero(funcs);
}

BOOL
js_async_resolve(JSContext* ctx, ResolveFunctions* funcs, JSValueConst value) {
  if(js_is_nullish(funcs->resolve))
    return FALSE;
  js_resolve_functions_call(ctx, funcs, 0, value);
  return TRUE;
}

BOOL
js_async_reject(JSContext* ctx, ResolveFunctions* funcs, JSValueConst value) {
  if(js_is_nullish(funcs->reject))
    return FALSE;
  js_resolve_functions_call(ctx, funcs, 1, value);
  return TRUE;
}

void
js_async_zero(ResolveFunctions* funcs) {
  js_resolve_functions_zero(funcs);
}

BOOL
js_async_pending(ResolveFunctions const* funcs) {
  return !js_resolve_functions_is_null(funcs);
}

JSValue
js_async_then(JSContext* ctx, JSValueConst promise, JSValueConst handler) {
  return js_invoke(ctx, promise, "then", 1, &handler);
}

JSValue
js_async_then2(JSContext* ctx, JSValueConst promise, JSValueConst onresolved, JSValueConst onrejected) {
  JSValueConst args[] = {onresolved, onrejected};
  return js_invoke(ctx, promise, "then", countof(args), args);
}

JSValue
js_async_catch(JSContext* ctx, JSValueConst promise, JSValueConst handler) {
  return js_invoke(ctx, promise, "catch", 1, &handler);
}

static THREAD_LOCAL JSClassID js_wrappedpromise_class_id;

JSWrappedPromiseRecord*
js_wrappedpromise_data(JSValueConst value) {
  return JS_GetOpaque(value, js_wrappedpromise_class_id);
}

JSWrappedPromiseRecord*
js_wrappedpromise_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_wrappedpromise_class_id);
}

enum { METHOD_THEN = 1, METHOD_CATCH = 2 };

static JSValue
js_wrappedpromise_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;
  JSWrappedPromiseRecord* wpr;

  if(!(wpr = js_wrappedpromise_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case METHOD_THEN: {
      ret = js_invoke(ctx, wpr->promise, "then", argc, argv);
      if(argc > 1)
        wpr->catched = TRUE;
      wpr->thened = TRUE;
      break;
    }
    case METHOD_CATCH: {
      ret = js_invoke(ctx, wpr->promise, "catch", argc, argv);
      wpr->catched = TRUE;
      break;
    }
  }

  return ret;
}

static JSValue
js_wrappedpromise_state(JSContext* ctx, JSValueConst this_val) {
  JSValue ret = JS_UNDEFINED;
  JSWrappedPromiseRecord* wpr;

  if(!(wpr = js_wrappedpromise_data2(ctx, this_val)))
    return JS_EXCEPTION;

  return JS_NewUint32(ctx, (wpr->thened ? METHOD_THEN : 0) | (wpr->catched ? METHOD_CATCH : 0));
}

static void
js_wrappedpromise_finalizer(JSRuntime* rt, JSValue val) {
  JSWrappedPromiseRecord* wpr;

  if((wpr = js_wrappedpromise_data(val))) {
    JS_FreeValueRT(rt, wpr->promise);
    js_free_rt(rt, wpr);
  }
}

static JSClassDef js_wrappedpromise_class = {
    .class_name = "JSWrappedPromise",
    .finalizer = js_wrappedpromise_finalizer,
};

static JSValue wrappedpromise_proto;

static const JSCFunctionListEntry js_wrappedpromise_functions[] = {
    JS_CFUNC_MAGIC_DEF("then", 1, js_wrappedpromise_methods, METHOD_THEN),
    JS_CFUNC_MAGIC_DEF("catch", 1, js_wrappedpromise_methods, METHOD_CATCH),
    JS_CGETSET_DEF("state", js_wrappedpromise_state, 0),
};

JSValue
js_promise_prototype(JSContext* ctx) {
  JSValue ret, promise, resolve_funcs[2];
  promise = JS_NewPromiseCapability(ctx, resolve_funcs);
  ret = JS_GetPrototype(ctx, promise);
  JS_FreeValue(ctx, promise);
  JS_FreeValue(ctx, resolve_funcs[0]);
  JS_FreeValue(ctx, resolve_funcs[1]);
  return ret;
}

JSValue
js_promise_wrap(JSContext* ctx, JSValueConst promise) {
  JSWrappedPromiseRecord* wpr;
  JSValue proto, obj;

  if(js_wrappedpromise_class_id == 0) {
    JS_NewClassID(&js_wrappedpromise_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_wrappedpromise_class_id, &js_wrappedpromise_class);
    JSValue promise_proto = js_promise_prototype(ctx);
    wrappedpromise_proto = JS_NewObjectProto(ctx, promise_proto);
    JS_FreeValue(ctx, promise_proto);
    JS_SetPropertyFunctionList(ctx, wrappedpromise_proto, js_wrappedpromise_functions, countof(js_wrappedpromise_functions));
  }

  obj = JS_NewObjectProtoClass(ctx, wrappedpromise_proto, js_wrappedpromise_class_id);

  if(JS_IsException(obj))
    return obj;

  if(!(wpr = js_malloc(ctx, sizeof(JSWrappedPromiseRecord)))) {
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }

  wpr->promise = JS_DupValue(ctx, promise);
  wpr->thened = FALSE;
  wpr->catched = FALSE;

  JS_SetOpaque(obj, wpr);

  return obj;
}

JSValue
js_arraybuffer_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  size_t size;
  uint8_t* ptr;

  if((ptr = JS_GetArrayBuffer(ctx, &size, argv[0])))
    return JS_NewStringLen(ctx, (const char*)ptr, size);

  return JS_ThrowTypeError(ctx, "ArrayBuffer expected");
}

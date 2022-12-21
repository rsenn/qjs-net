#define _GNU_SOURCE
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "jsutils.h"
#include "buffer.h"

JSValue
vector2array(JSContext* ctx, int argc, JSValueConst argv[]) {
  int i;
  JSValue ret = JS_NewArray(ctx);
  for(i = 0; i < argc; i++) JS_SetPropertyUint32(ctx, ret, i, argv[i]);
  return ret;
}

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

void
js_console_log(JSContext* ctx, JSValue* console, JSValue* console_log) {
  JSValue global = JS_GetGlobalObject(ctx);
  *console = JS_GetPropertyStr(ctx, global, "console");
  *console_log = JS_GetPropertyStr(ctx, *console, "log");
  JS_FreeValue(ctx, global);
}

JSValue
js_function_bound(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue* func_data) {
  BOOL bind_this = !!(magic & JS_BIND_THIS);
  int i = 0, k = 0, count = magic & ~(JS_BIND_THIS);
  JSValue args[argc + count], fn;

  fn = *func_data++;

  if(bind_this)
    this_val = func_data[i++];

  for(; i < count; i++) args[k++] = func_data[i];

  for(i = 0; i < argc; i++) args[k++] = argv[i];

  return JS_Call(ctx, fn, this_val, k, args);
}

JSValue
js_function_bind(JSContext* ctx, JSValueConst func, int flags, JSValueConst argv[]) {
  int i, argc = flags & ~(JS_BIND_THIS);
  JSValue data[argc + 1];

  data[0] = JS_DupValue(ctx, func);
  for(i = 0; i < argc; i++) data[i + 1] = JS_DupValue(ctx, argv[i]);

  return JS_NewCFunctionData(ctx, js_function_bound, 0, flags, argc + 1, data);
}

JSValue
js_function_bind_1(JSContext* ctx, JSValueConst func, JSValueConst arg) {
  return js_function_bind(ctx, func, 1, &arg);
}

JSValue
js_function_bind_this(JSContext* ctx, JSValueConst func, JSValueConst this_val) {
  return js_function_bind(ctx, func, 1 | JS_BIND_THIS, &this_val);
}

JSValue
js_function_bind_this_1(JSContext* ctx, JSValueConst func, JSValueConst this_val, JSValueConst arg) {
  JSValueConst bound[] = {this_val, arg};
  return js_function_bind(ctx, func, countof(bound) | JS_BIND_THIS, &bound);
}

/*JSValue
js_function_bind_v(JSContext* ctx, JSValueConst func, ...) {
  va_list args;
  DynBuf b;
  JSValueConst arg;
  dbuf_init2(&b, ctx, (DynBufReallocFunc*)js_realloc);
  va_start(args, func);
  while((arg = va_arg(args, JSValueConst))) { dbuf_put(&b, &arg, sizeof(JSValueConst)); }
  va_end(args);
  return js_function_bind(ctx, func, b.size / sizeof(JSValueConst), (JSValueConst*)b.buf);
}*/

const char*
js_function_name(JSContext* ctx, JSValueConst value) {
  JSValue str, name, args[2], idx;
  const char* s = 0;
  int32_t i = -1;
  str = js_invoke(ctx, value, "toString", 0, 0);
  args[0] = JS_NewString(ctx, "function ");
  idx = js_invoke(ctx, str, "indexOf", 1, args);
  JS_FreeValue(ctx, args[0]);
  JS_ToInt32(ctx, &i, idx);
  if(i != 0) {
    JS_FreeValue(ctx, str);
    return 0;
  }
  args[0] = JS_NewString(ctx, "(");
  idx = js_invoke(ctx, str, "indexOf", 1, args);
  JS_FreeValue(ctx, args[0]);
  args[0] = JS_NewUint32(ctx, 9);
  args[1] = idx;
  name = js_invoke(ctx, str, "substring", 2, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, str);
  s = JS_ToCString(ctx, name);
  JS_FreeValue(ctx, name);
  return s;
}

JSValue
js_iterator_result(JSContext* ctx, JSValueConst value, BOOL done) {
  JSValue ret = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, ret, "done", JS_NewBool(ctx, done));
  JS_SetPropertyStr(ctx, ret, "value", JS_DupValue(ctx, value));

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
js_buffer_data(JSContext* ctx, const void* data, size_t size) {
  ByteBlock block = {(uint8_t*)data, (uint8_t*)data + size};

  return js_buffer_fromblock(ctx, &block);
}

JSBuffer
js_buffer_alloc(JSContext* ctx, size_t size) {
  ByteBlock block = {0, 0};

  if((block.start = js_malloc(ctx, size)))
    block.end = block.start + size;

  return js_buffer_fromblock(ctx, &block);
}

void
js_buffer_to(JSBuffer buf, void** pptr, size_t* plen) {
  if(pptr)
    *pptr = buf.data;
  if(plen)
    *plen = buf.size;
}

void
js_buffer_to3(JSBuffer buf, const char** pstr, void** pptr, unsigned* plen) {
  if(!JS_IsString(buf.value)) {
    size_t len = 0;
    js_buffer_to(buf, pptr, &len);
    if(plen)
      *plen = len;
  } else
    *pstr = (const char*)buf.data;
}

BOOL
js_buffer_valid(const JSBuffer* in) {
  return !JS_IsException(in->value);
}

JSBuffer
js_buffer_clone(const JSBuffer* in, JSContext* ctx) {
  JSBuffer buf = js_input_buffer(ctx, in->value);
  buf.pos = in->pos;
  buf.range = in->range;
  return buf;
}

void
js_buffer_dump(const JSBuffer* in, DynBuf* db) {
  dbuf_printf(db, "(JSBuffer){ .data = %p, .size = %zu, .free = %p }", in->data, in->size, in->free);
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
  js_buffer_free(in, JS_GetRuntime(ctx));
}

BOOL
js_is_iterable(JSContext* ctx, JSValueConst obj) {
  JSAtom atom;
  BOOL ret = FALSE;
  atom = js_symbol_static_atom(ctx, "iterator");
  if(JS_HasProperty(ctx, obj, atom))
    ret = TRUE;

  JS_FreeAtom(ctx, atom);
  if(!ret) {
    atom = js_symbol_static_atom(ctx, "asyncIterator");
    if(JS_HasProperty(ctx, obj, atom))
      ret = TRUE;

    JS_FreeAtom(ctx, atom);
  }
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
js_is_async_generator(JSContext* ctx, JSValueConst obj) {
  BOOL ret = FALSE;
  const char* str;

  if((str = JS_ToCString(ctx, obj))) {
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
js_timer_callback(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, void* opaque) {
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
  closure->callback = JS_NewCClosure(ctx, js_timer_callback, 0, 0, closure, js_timer_free);
  closure->id = js_timer_start(ctx, closure->callback, ms);

  return closure;
}

void
js_timer_restart(struct TimerClosure* closure) {
  js_timer_cancel(closure->ctx, closure->id);
  JS_FreeValue(closure->ctx, closure->id);
  closure->id = js_timer_start(closure->ctx, closure->callback, closure->interval);
}

static inline void
js_resolve_functions_zero(ResolveFunctions* funcs) {
  funcs->array[0] = JS_NULL;
  funcs->array[1] = JS_NULL;
}

static inline BOOL
js_resolve_functions_is_null(ResolveFunctions const* funcs) {
  return JS_IsNull(funcs->array[0]) && JS_IsNull(funcs->array[1]);
}

static inline JSValue
js_resolve_functions_call(JSContext* ctx, ResolveFunctions* funcs, int index, JSValueConst arg) {
  JSValue ret = JS_UNDEFINED;
  assert(!JS_IsNull(funcs->array[index]));
  ret = JS_Call(ctx, funcs->array[index], JS_UNDEFINED, 1, &arg);
  js_promise_free(ctx, funcs);
  return ret;
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

JSValue
js_promise_create(JSContext* ctx, ResolveFunctions* funcs) {
  JSValue ret;

  ret = JS_NewPromiseCapability(ctx, funcs->array);
  return ret;
}

void
js_promise_free(JSContext* ctx, ResolveFunctions* funcs) {
  JS_FreeValue(ctx, funcs->array[0]);
  JS_FreeValue(ctx, funcs->array[1]);
  js_resolve_functions_zero(funcs);
}

void
js_promise_free_rt(JSRuntime* rt, ResolveFunctions* funcs) {
  JS_FreeValueRT(rt, funcs->array[0]);
  JS_FreeValueRT(rt, funcs->array[1]);
  js_resolve_functions_zero(funcs);
}

JSValue
js_promise_resolve(JSContext* ctx, ResolveFunctions* funcs, JSValueConst value) {
  if(js_is_nullish(funcs->array[0]))
    return JS_UNDEFINED;
  return js_resolve_functions_call(ctx, funcs, 0, value);
}

JSValue
js_promise_reject(JSContext* ctx, ResolveFunctions* funcs, JSValueConst value) {
  if(js_is_nullish(funcs->array[1]))
    return JS_UNDEFINED;
  return js_resolve_functions_call(ctx, funcs, 1, value);
}

void
js_promise_zero(ResolveFunctions* funcs) {
  js_resolve_functions_zero(funcs);
}

BOOL
js_promise_pending(ResolveFunctions const* funcs) {
  return !js_resolve_functions_is_null(funcs);
}

BOOL
js_promise_done(ResolveFunctions const* funcs) {
  return js_resolve_functions_is_null(funcs);
}

JSValue
js_promise_then(JSContext* ctx, JSValueConst promise, JSValueConst handler) {
  return js_invoke(ctx, promise, "then", 1, &handler);
}

BOOL
js_is_promise(JSContext* ctx, JSValueConst value) {
  JSValue ctor;
  BOOL ret;

  ctor = js_global_get(ctx, "Promise");
  ret = JS_IsInstanceOf(ctx, value, ctor);

  JS_FreeValue(ctx, ctor);
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
  }
  if(stack) {
    size_t pos = 0, i = 0, len, end = strlen(stack);
    lwsl_err("Stack:");

    while(i < end) {
      len = byte_chrs(&stack[i], end - i, "\r\n", 2);

      lwsl_err("%zu: %.*s", pos++, (int)len, &stack[i]);
      i += len;

      i += scan_charsetnskip(&stack[i], "\r\n", end - i);
    }
  }
  if(stack)
    JS_FreeCString(ctx, stack);
  if(str)
    JS_FreeCString(ctx, str);
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

int64_t
js_get_propertystr_int64(JSContext* ctx, JSValueConst obj, const char* str) {
  int64_t ret = 0;
  JSValue value;
  value = JS_GetPropertyStr(ctx, obj, str);
  JS_ToInt64(ctx, &ret, value);
  JS_FreeValue(ctx, value);
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

BOOL
js_has_propertystr(JSContext* ctx, JSValueConst obj, const char* str) {
  JSAtom prop = JS_NewAtom(ctx, str);
  BOOL ret = JS_HasProperty(ctx, obj, prop);
  JS_FreeAtom(ctx, prop);
  return ret;
}

struct list_head*
js_module_list(JSContext* ctx) {
  void* tmp_opaque;
  ptrdiff_t needle;
  void** ptr;
  tmp_opaque = JS_GetContextOpaque(ctx);
  memset(&needle, 0xa5, sizeof(needle));
  JS_SetContextOpaque(ctx, (void*)needle);

  ptr = memmem(ctx, 1024, &needle, sizeof(needle));

  JS_SetContextOpaque(ctx, tmp_opaque);

  return ((struct list_head*)(ptr - 2)) - 1;
}

JSModuleDef*
js_module_at(JSContext* ctx, int i) {
  struct list_head *el = 0, *list = js_module_list(ctx);

  list_for_each(list, el) {
    JSModuleDef* module = (void*)((char*)el - sizeof(JSAtom) * 2);

    if(i-- == 0)
      return module;
  }
  return 0;
}

JSModuleDef*
js_module_find(JSContext* ctx, JSAtom name) {
  struct list_head *el, *list = js_module_list(ctx);

  list_for_each(el, list) {
    JSModuleDef* module = (void*)((char*)el - sizeof(JSAtom) * 2);

    if(((JSAtom*)module)[1] == name)
      return module;
  }
  return 0;
}

JSModuleDef*
js_module_find_s(JSContext* ctx, const char* name) {
  JSAtom atom;
  JSModuleDef* module;
  atom = JS_NewAtom(ctx, name);
  module = js_module_find(ctx, atom);
  JS_FreeAtom(ctx, atom);
  return module;
}

void*
js_module_export_find(JSModuleDef* module, JSAtom name) {
  void* export_entries = *(void**)((char*)module + sizeof(int) * 2 + sizeof(struct list_head) + sizeof(void*) + sizeof(int) * 2);
  int export_entries_count = *(int*)((char*)module + sizeof(int) * 2 + sizeof(struct list_head) + sizeof(void*) + sizeof(int) * 2 + sizeof(void*));
  static const size_t export_entry_size = sizeof(void*) * 2 + sizeof(int) * 2;
  size_t i;

  for(i = 0; i < export_entries_count; i++) {
    void* entry = (char*)export_entries + export_entry_size * i;

    JSAtom* export_name = (JSAtom*)(char*)entry + sizeof(void*) * 2 + sizeof(int) * 2;

    if(*export_name == name)
      return entry;
  }

  return 0;
}

extern JSModuleDef* js_module_loader(JSContext* ctx, const char* module_name, void* opaque);

JSValue
js_module_import_meta(JSContext* ctx, const char* name) {
  JSModuleDef* m;
  JSValue ret = JS_UNDEFINED;

  if((m = js_module_loader(ctx, name, 0))) {
    ret = JS_GetImportMeta(ctx, m);
  }
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

int64_t
js_arraybuffer_length(JSContext* ctx, JSValueConst buffer) {
  size_t len;

  if(JS_GetArrayBuffer(ctx, &len, buffer))
    return len;

  return -1;
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
    for(i = 0; argv[i]; i++) JS_SetPropertyUint32(ctx, ret, i, JS_NewString(ctx, argv[i]));
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

BOOL
js_atom_is_string(JSContext* ctx, JSAtom atom) {
  JSValue value;
  BOOL ret;
  value = JS_AtomToValue(ctx, atom);
  ret = JS_IsString(value);
  JS_FreeValue(ctx, value);
  return ret;
}

JSBuffer
js_input_buffer(JSContext* ctx, JSValueConst value) {
  JSBuffer ret = {0, 0, 0, &js_buffer_free_default, JS_UNDEFINED};
  int64_t offset = 0, length = INT64_MAX;

  ol_init(&ret.range);

  if(js_is_typedarray(ctx, value) || js_is_dataview(ctx, value)) {
    JSValue arraybuf, byteoffs, bytelen;
    arraybuf = JS_GetPropertyStr(ctx, value, "buffer");
    bytelen = JS_GetPropertyStr(ctx, value, "byteLength");
    if(JS_IsNumber(bytelen))
      JS_ToInt64(ctx, &length, bytelen);
    JS_FreeValue(ctx, bytelen);
    byteoffs = JS_GetPropertyStr(ctx, value, "byteOffset");
    if(JS_IsNumber(byteoffs))
      JS_ToInt64(ctx, &offset, byteoffs);
    JS_FreeValue(ctx, byteoffs);
    value = arraybuf;
  }

  if(js_is_arraybuffer(ctx, value)) {
    ret.value = JS_DupValue(ctx, value);
    ret.data = JS_GetArrayBuffer(ctx, &ret.size, ret.value);
  } else {
    ret.value = JS_EXCEPTION;
    // JS_ThrowTypeError(ctx, "Invalid type for input buffer");
  }

  if(offset < 0)
    ret.range.offset = ret.size + offset % ret.size;
  else if(offset > ret.size)
    ret.range.offset = ret.size;
  else
    ret.range.offset = offset;

  if(length >= 0 && length < ret.size)
    ret.range.length = length;

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

BOOL
js_is_generator(JSContext* ctx, JSValueConst value) {
  const char* str;
  BOOL ret = FALSE;

  if((str = JS_ToCString(ctx, value))) {
    const char* s = str;

    if(!strncmp(s, "async ", 6))
      s += 6;

    if(!strncmp(s, "function", 8)) {
      s += 8;

      while(*s == ' ') ++s;

      if(*s == '*')
        ret = TRUE;
    }

    JS_FreeCString(ctx, str);
  }
  return ret;
}

BOOL
js_is_async(JSContext* ctx, JSValueConst value) {
  const char* str;
  BOOL ret = FALSE;
  if((str = JS_ToCString(ctx, value))) {
    const char* s = str;

    if(!strncmp(s, "async ", 6))
      ret = TRUE;

    else if(!strncmp(s, "[object Async", 13))
      ret = TRUE;

    JS_FreeCString(ctx, str);
  }
  return ret;
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

#define _GNU_SOURCE
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "jsutils.h"

JSValue
vector2array(JSContext* ctx, int argc, JSValueConst argv[]) {
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

JSValue
js_iterator_next(JSContext* ctx, JSValueConst obj, JSValue* next, BOOL* done_p, int argc, JSValueConst argv[]) {
  JSValue fn, result, done, value;

  if(!JS_IsObject(obj))
    return JS_ThrowTypeError(ctx, "argument is not an object (%d) \"%s\"", JS_VALUE_GET_TAG(obj), JS_ToCString(ctx, obj));

  if(JS_IsObject(*next) && JS_IsFunction(ctx, *next)) {
    fn = *next;
  } else {
    fn = JS_GetPropertyStr(ctx, obj, "next");

    if(JS_IsUndefined(fn))
      return JS_ThrowTypeError(ctx, "object does not have 'next' method");

    if(!JS_IsFunction(ctx, fn))
      return JS_ThrowTypeError(ctx, "object.next is not a function");

    *next = fn;
  }

  result = JS_Call(ctx, fn, obj, argc, argv);
  // JS_FreeValue(ctx, fn);

  if(JS_IsException(result))
    return JS_EXCEPTION;

  done = JS_GetPropertyStr(ctx, result, "done");
  value = JS_GetPropertyStr(ctx, result, "value");
  JS_FreeValue(ctx, result);
  *done_p = JS_ToBool(ctx, done);
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
js_buffer_from(JSContext* ctx, JSBuffer* buf, JSValueConst value) {
  buf->data = 0;
  buf->size = 0;
  buf->free = &js_buffer_free_default;
  buf->value = JS_UNDEFINED;

  if(JS_IsString(value)) {
    buf->data = (uint8_t*)JS_ToCStringLen(ctx, &buf->size, value);
    buf->value = value;
  } else if((buf->data = JS_GetArrayBuffer(ctx, &buf->size, value))) {
    buf->value = JS_DupValue(ctx, value);
  }
}

JSBuffer
js_buffer_new(JSContext* ctx, JSValueConst value) {
  JSBuffer ret = {0, 0, &js_buffer_free_default, JS_UNDEFINED};
  ret.free = &js_buffer_free_default;

  if(JS_IsString(value)) {
    ret.data = (uint8_t*)JS_ToCStringLen(ctx, &ret.size, value);
    ret.value = value;
  } else if((ret.data = JS_GetArrayBuffer(ctx, &ret.size, value))) {
    ret.value = JS_DupValue(ctx, value);
  }

  return ret;
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
  JSBuffer ret;

  js_buffer_from(ctx, &ret, in->value);

  /*  ret.size = in->size;
   ret.free = in->free;*/

  return ret;
}

void
js_buffer_dump(const JSBuffer* in, DynBuf* db) {
  dbuf_printf(db, "(JSBuffer){ .data = %p, .size = %zu, .free = %p }", in->data, in->size, in->free);
}

void
js_buffer_free(JSBuffer* in, JSContext* ctx) {
  if(in->data) {
    in->free(JS_GetRuntime(ctx), in, in->data);
    in->data = 0;
    in->size = 0;
    in->value = JS_UNDEFINED;
  }
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
  JSFreeValue(ctx, set_timeout);
  return ret;
}

void
js_timer_cancel(JSContext* ctx, JSValueConst timer) {
  JSValue clear_timeout = js_os_get(ctx, "clearTimeout");
  JSValue ret = JS_Call(ctx, clear_timeout, JS_UNDEFINED, 1, &timer);
  JSFreeValue(ctx, clear_timeout);
  JSFreeValue(ctx, ret);
}

void
js_timer_free(void* ptr) {
  struct TimerClosure* closure = ptr;
  JSContext* ctx;

  JS_FreeValue(ctx, closure->id);
  JS_FreeValue(ctx, closure->handler);
  JS_FreeValue(ctx, closure->callback);

  js_free(ctx, closure);
}

JSValue
js_timer_callback(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, void* opaque) {
  struct TimerClosure* closure = opaque;
  JSValue ret;
  ret = JS_Call(ctx, closure->handler, this_val, argc, argv);
  
  JS_FreeValue(ctx, closure->id);
  closure->id = js_timer_start(ctx, closure->callback, closure->interval);
  
  return ret;
}

struct TimerClosure*
js_os_interval(JSContext* ctx, JSValueConst fn, uint32_t ms) {
  struct TimerClosure* closure;
  JSValue callback, id_value;

  if(!(closure = js_malloc(ctx, sizeof(struct TimerClosure))))
    return 0;

  closure->ctx = ctx;
  closure->interval = ms;
  closure->handler = JS_DupValue(ctx, fn);

  callback = JS_NewCClosure(ctx, js_timer_callback, 0, 0, closure, js_timer_free);

  closure->id = js_timer_start(ctx, callback, ms);
  closure->callback = callback;

  return closure;
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

void
js_promise_free(JSContext* ctx, ResolveFunctions* funcs) {
  JS_FreeValue(ctx, funcs->array[0]);
  JS_FreeValue(ctx, funcs->array[1]);
  js_resolve_functions_zero(funcs);
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

JSValue
js_promise_resolve(JSContext* ctx, ResolveFunctions* funcs, JSValueConst value) {
  return js_resolve_functions_call(ctx, funcs, 0, value);
}

JSValue
js_promise_reject(JSContext* ctx, ResolveFunctions* funcs, JSValueConst value) {
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

struct list_head*
js_module_list(JSContext* ctx) {
  void* tmp_opaque;
  ptrdiff_t needle;
  void** ptr;
  tmp_opaque = JS_GetContextOpaque(ctx);
  memset(&needle, 0xa5, sizeof(needle));
  JS_SetContextOpaque(ctx, needle);

  ptr = memmem(ctx, 1024, &needle, sizeof(needle));
  printf("ctx = %p\n", ctx);
  printf("&needle = %p\n", &needle);
  printf("ptr = %p\n", ptr);
  printf("ctx.user_opaque = %016zx\n", (char*)ptr - (char*)ctx);
  JS_SetContextOpaque(ctx, tmp_opaque);

  return ((struct list_head*)(ptr - 2)) - 1;
}

JSModuleDef*
js_module_at(JSContext* ctx, int i) {
  struct list_head *el, *list = js_module_list(ctx);

  list_for_each(list, el) {
    JSModuleDef* module = (char*)el - sizeof(JSAtom) * 2;

    if(i-- == 0)
      return module;
  }
  return 0;
}

JSModuleDef*
js_module_find(JSContext* ctx, JSAtom name) {
  struct list_head *el, *list = js_module_list(ctx);

  list_for_each(el, list) {
    JSModuleDef* module = (char*)el - sizeof(JSAtom) * 2;

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

    JSAtom* export_name = (char*)entry + sizeof(void*) * 2 + sizeof(int) * 2;

    if(*export_name == name)
      return entry;
  }

  return 0;
}

JSValue
js_module_import_meta(JSContext* ctx, const char* name) {
  JSModuleDef* m;
  JSValue ret = JS_UNDEFINED;

  if((m = js_module_loader(ctx, name, 0))) {
    ret = JS_GetImportMeta(ctx, m);
  }
  return ret;
}

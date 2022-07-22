#ifndef MINNET_JSUTILS_H
#define MINNET_JSUTILS_H

#include <quickjs.h>
#include <cutils.h>
#include <list.h>

typedef struct JSThreadState {
  struct list_head os_rw_handlers;
  struct list_head os_signal_handlers;
  struct list_head os_timers;
  struct list_head port_list;
  int eval_script_recurse;
  void *recv_pipe, *send_pipe;
} JSThreadState;

typedef struct input_buffer {
  uint8_t* data;
  size_t size;
  void (*free)(JSRuntime*, void* opaque, void* ptr);
  JSValue value;
} JSBuffer;

#define JS_BUFFER(data, size, free) \
  (JSBuffer) { (data), (size), (free), JS_UNDEFINED }

typedef union resolve_functions {
  JSValue array[2];
  struct {
    JSValue resolve, reject;
  };
} ResolveFunctions;

struct TimerClosure {
  int ref_count;
  uint32_t interval;
  JSContext* ctx;
  JSValueConst id, handler, callback;
};

#define JS_BIND_THIS 0x8000

JSValue vector2array(JSContext*, int, JSValueConst[]);
JSValue js_object_constructor(JSContext* ctx, JSValueConst value);
void js_console_log(JSContext*, JSValueConst*, JSValueConst*);
JSValue js_function_bound(JSContext*, JSValueConst, int, JSValueConst argv[], int magic, JSValueConst* func_data);
JSValue js_function_bind(JSContext*, JSValueConst, int, JSValueConst argv[]);
JSValue js_function_bind_1(JSContext*, JSValueConst, JSValueConst);
JSValue js_function_bind_this(JSContext*, JSValueConst, JSValueConst);
const char* js_function_name(JSContext*, JSValueConst);
JSValue js_iterator_next(JSContext*, JSValueConst, JSValueConst* next, BOOL* done_p, int argc, JSValueConst argv[]);
int js_copy_properties(JSContext*, JSValueConst, JSValueConst, int flags);
void js_buffer_from(JSContext*, JSBuffer*, JSValueConst);
JSBuffer js_buffer_new(JSContext*, JSValueConst);
void js_buffer_to(JSBuffer, void**, size_t*);
void js_buffer_to3(JSBuffer, const char**, void**, unsigned* plen);
BOOL js_buffer_valid(const JSBuffer*);
JSBuffer js_buffer_clone(const JSBuffer*, JSContext*);
void js_buffer_dump(const JSBuffer*, DynBuf*);
void js_buffer_free(JSBuffer*, JSContext*);
BOOL js_is_iterable(JSContext*, JSValueConst);
BOOL js_is_iterator(JSContext*, JSValueConst);
JSAtom js_symbol_static_atom(JSContext*, const char*);
JSValue js_symbol_static_value(JSContext*, const char*);
JSValue js_symbol_ctor(JSContext*);
JSValue js_global_get(JSContext*, const char*);
JSValue js_global_os(JSContext*);
JSValue js_os_get(JSContext*, const char*);
JSValue js_timer_start(JSContext*, JSValueConst, uint32_t);
void js_timer_cancel(JSContext*, JSValueConst);
void js_timer_free(void*);
JSValue js_timer_callback(JSContext*, JSValueConst, int, JSValueConst* argv, int magic, void* opaque);
struct TimerClosure* js_timer_interval(JSContext*, JSValueConst, uint32_t);
void js_timer_restart(struct TimerClosure*);
void js_promise_free(JSContext*, ResolveFunctions*);
void js_promise_free_rt(JSRuntime*, ResolveFunctions*);
char* js_tostringlen(JSContext*, size_t*, JSValueConst);
char* js_tostring(JSContext*, JSValueConst);
JSValue js_invoke(JSContext*, JSValueConst, const char*, int argc, JSValueConst argv[]);
JSValue js_promise_create(JSContext*, ResolveFunctions*);
JSValue js_promise_resolve(JSContext*, ResolveFunctions*, JSValueConst);
JSValue js_promise_reject(JSContext*, ResolveFunctions*, JSValueConst);
void js_promise_zero(ResolveFunctions*);
BOOL js_promise_pending(ResolveFunctions const*);
BOOL js_promise_done(ResolveFunctions const*);
BOOL js_is_promise(JSContext*, JSValueConst);
JSValue js_error_new(JSContext*, const char*, ...);
uint8_t* js_toptrsize(JSContext*, unsigned int*, JSValueConst);
BOOL js_get_propertystr_bool(JSContext*, JSValueConst, const char*);
int64_t js_get_propertystr_int64(JSContext*, JSValueConst, const char*);
uint32_t js_get_propertystr_uint32(JSContext*, JSValueConst, const char*);
struct list_head* js_module_list(JSContext*);
JSModuleDef* js_module_at(JSContext*, int);
JSModuleDef* js_module_find(JSContext*, JSAtom);
JSModuleDef* js_module_find_s(JSContext*, const char*);
void* js_module_export_find(JSModuleDef*, JSAtom);
JSValue js_module_import_meta(JSContext*, const char*);
void js_error_print(JSContext*, JSValueConst);

static inline void
js_clear(JSContext* ctx, const void* arg) {
  const void** ptr = (const void**)arg;
  if(*ptr)
    js_free(ctx, (void*)*ptr);
  *ptr = 0;
}

static inline void
js_dump_string(const char* str, size_t len, size_t maxlen) {
  size_t i, n = 2;
  putchar('\'');
  for(i = 0; i < len; i++) {
    if(str[i] == '\n') {
      putchar('\\');
      putchar('n');
      n += 2;
    } else {
      putchar(str[i]);
      n++;
    }
    if(maxlen > 0 && n + 1 >= maxlen) {
      fputs("'...", stdout);
      return;
    }
  }
  putchar('\'');
}

static inline char*
js_to_string(JSContext* ctx, JSValueConst value) {
  const char* s;
  char* ret = 0;

  if((s = JS_ToCString(ctx, value))) {
    ret = js_strdup(ctx, s);
    JS_FreeCString(ctx, s);
  }
  return ret;
}

static inline char*
js_replace_string(JSContext* ctx, JSValueConst value, char** sptr) {
  const char* s;

  if(*sptr)
    js_free(ctx, *sptr);

  if((s = JS_ToCString(ctx, value))) {
    *sptr = js_strdup(ctx, s);
    JS_FreeCString(ctx, s);
  }
  return *sptr;
}

static inline BOOL
js_is_nullish(JSValueConst value) {
  return JS_IsNull(value) || JS_IsUndefined(value);
}

static inline void
js_buffer_free_default(JSRuntime* rt, void* opaque, void* ptr) {
  JSBuffer* buf = opaque;

  if(JS_IsString(buf->value))
    JS_FreeValueRT(rt, buf->value);
  else if(!JS_IsUndefined(buf->value))
    JS_FreeValueRT(rt, buf->value);
}

static inline const uint8_t*
js_buffer_begin(const JSBuffer* in) {
  return in->data;
}

static inline const uint8_t*
js_buffer_end(const JSBuffer* in) {
  return in->data + in->size;
}

typedef struct async_read {
  struct list_head link;
  ResolveFunctions promise;
} AsyncRead;

typedef struct value_item {
  struct list_head link;
  JSValue value;
  int64_t id;
} ValueItem;

typedef struct async_iterator {
  JSContext* ctx;
  struct list_head reads /*, values*/;
} AsyncIterator;

void asynciterator_zero(AsyncIterator*);
void asynciterator_clear(AsyncIterator*, JSRuntime*);
AsyncIterator* asynciterator_new(JSContext*);
JSValue asynciterator_yield(AsyncIterator*, JSContext*);
AsyncRead* asynciterator_read(AsyncIterator*, JSContext*);
int64_t asynciterator_push(AsyncIterator*, JSValueConst, JSContext*);

#endif /* MINNET_JS_UTILS_H */

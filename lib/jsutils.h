#ifndef QJSNET_LIB_JSUTILS_H
#define QJSNET_LIB_JSUTILS_H

#include <quickjs.h>
#include <cutils.h>
#include <list.h>
#include "utils.h"

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

#define JS_CGETSET_MAGIC_FLAGS_DEF(prop_name, fgetter, fsetter, magic_num, flags) \
  { \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET_MAGIC, .magic = magic_num, .u = {.getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} } \
  }
#define JS_CGETSET_FLAGS_DEF(prop_name, fgetter, fsetter, flags) \
  { \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET, .u = {.getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} } \
  }

#define JS_INDEX_STRING_DEF(index, cstr) \
  { \
    .name = #index, .prop_flags = JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE, .def_type = JS_DEF_PROP_STRING, .magic = 0, .u = {.str = cstr } \
  }
#define JS_CFUNC_FLAGS_DEF(prop_name, length, func1, flags) \
  { \
    .name = prop_name, .prop_flags = (flags), .def_type = JS_DEF_CFUNC, .magic = 0, .u = {.func = {length, JS_CFUNC_generic, {.generic = func1}} } \
  }

typedef enum { ON_RESOLVE = 0, ON_REJECT } promise_handler_e;

typedef struct JSThreadState {
  struct list_head os_rw_handlers;
  struct list_head os_signal_handlers;
  struct list_head os_timers;
  struct list_head port_list;
  int eval_script_recurse;
  void *recv_pipe, *send_pipe;
} JSThreadState;

typedef struct offset_length {
  int64_t offset, length;
} OffsetLength;

typedef struct input_buffer {
  uint8_t* data;
  size_t size;
  size_t pos;
  void (*free)(JSRuntime*, void* opaque, void* ptr);
  JSValue value;
  OffsetLength range;
} JSBuffer;

#define JS_BUFFER_DEFAULT() JS_BUFFER(0, 0, &js_buffer_free_default)

#define JS_BUFFER_0(free) JS_BUFFER(0, 0, (free))

#define JS_BUFFER(data, size, free) \
  (JSBuffer) { \
    (data), (size), 0, (free), JS_UNDEFINED, { 0, -1 } \
  }

#define JS_BUFFER_VALUE(data, size, value) \
  (JSBuffer) { \
    (data), (size), 0, (&js_buffer_free_default), (value), { 0, -1 } \
  }

typedef struct key_value {
  JSAtom key;
  JSValue value;
} JSEntry;

#define JS_ENTRY() \
  (JSEntry) { -1, JS_UNDEFINED }

typedef struct resolve_functions {
  JSValue resolve, reject;
} ResolveFunctions;

struct TimerClosure {
  int ref_count;
  uint32_t interval;
  JSContext* ctx;
  JSValueConst id, handler, callback;
};

#define JS_BIND_THIS 0x8000

static inline void
js_vector_free(JSContext* ctx, int argc, JSValue argv[]) {
  for(int i = 0; i < argc; i++) {
    JS_FreeValue(ctx, argv[i]);
    argv[i] = JS_UNDEFINED;
  }
}

struct byte_block;
JSValue js_object_constructor(JSContext*, JSValueConst value);
char* js_object_classname(JSContext*, JSValueConst value);
void js_console_log(JSContext*, JSValueConst* console, JSValueConst* console_log);
JSValue js_function_bound(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValueConst* func_data);
JSValue js_function_bind(JSContext*, JSValueConst func, int flags, JSValueConst argv[]);
JSValue js_function_bind_1(JSContext*, JSValueConst func, JSValueConst arg);
JSValue js_function_bind_this(JSContext*, JSValueConst func, JSValueConst this_val);
JSValue js_function_bind_this_1(JSContext*, JSValueConst func, JSValueConst this_val, JSValueConst arg);
const char* js_function_name(JSContext*, JSValueConst value);
JSValue js_function_prototype(JSContext*);
JSValue js_iterator_result(JSContext*, JSValueConst value, BOOL done);
JSValue js_iterator_next(JSContext*, JSValueConst obj, JSValueConst* next, BOOL* done_p, int argc, JSValueConst argv[]);
int js_copy_properties(JSContext*, JSValueConst dst, JSValueConst src, int flags);
void js_buffer_free_default(JSRuntime*, void* opaque, void* ptr);
BOOL js_buffer_from(JSContext*, JSBuffer* buf, JSValueConst value);
JSBuffer js_buffer_new(JSContext*, JSValueConst value);
JSBuffer js_buffer_fromblock(JSContext*, struct byte_block* blk);
JSBuffer js_buffer_alloc(JSContext*, size_t size);
void js_buffer_free_rt(JSBuffer*, JSRuntime* rt);
void js_buffer_free(JSBuffer*, JSContext* ctx);
BOOL js_is_iterator(JSContext*, JSValueConst obj);
BOOL js_is_async_generator(JSContext*, JSValueConst obj);
JSAtom js_symbol_static_atom(JSContext*, const char* name);
JSValue js_symbol_static_value(JSContext*, const char* name);
JSValue js_symbol_for_value(JSContext*, const char* name);
JSAtom js_symbol_for_atom(JSContext*, const char* name);
JSValue js_symbol_ctor(JSContext*);
JSValue js_global_get(JSContext*, const char* prop);
JSValue js_global_os(JSContext*);
JSValue js_os_get(JSContext*, const char* prop);
JSValue js_timer_start(JSContext*, JSValueConst fn, uint32_t ms);
void js_timer_cancel(JSContext*, JSValueConst timer);
void js_timer_free(void*);
JSValue js_timer_callback(JSContext*, JSValueConst this_val, int argc, JSValueConst* argv, int magic, void* opaque);
struct TimerClosure* js_timer_interval(JSContext*, JSValueConst fn, uint32_t ms);
void js_timer_restart(struct TimerClosure*);
char* js_tostringlen(JSContext*, size_t* lenp, JSValueConst value);
char* js_tostring(JSContext*, JSValueConst value);
JSValue js_invoke(JSContext*, JSValueConst this_obj, const char* method, int argc, JSValueConst argv[]);
JSValue js_async_new(JSContext* ctx, JSValue* resolve, JSValue* reject);
JSValue js_async_create(JSContext*, ResolveFunctions* funcs);
void js_async_free(JSContext*, ResolveFunctions* funcs);
void js_async_free_rt(JSRuntime*, ResolveFunctions* funcs);
JSValue js_async_resolve(JSContext*, ResolveFunctions* funcs, JSValueConst value);
JSValue js_async_reject(JSContext*, ResolveFunctions* funcs, JSValueConst value);
void js_async_zero(ResolveFunctions*);
BOOL js_async_pending(ResolveFunctions const*);
JSValue js_async_then(JSContext*, JSValueConst promise, JSValueConst handler);
JSValue js_async_catch(JSContext*, JSValueConst promise, JSValueConst handler);
BOOL js_is_promise(JSContext*, JSValueConst value);
JSValue js_error_new(JSContext*, const char* fmt, ...);
void js_error_print(JSContext*, JSValueConst error);
uint8_t* js_toptrsize(JSContext*, unsigned int* plen, JSValueConst value);
uint32_t js_get_propertystr_uint32(JSContext*, JSValueConst obj, const char* str);
BOOL js_has_propertystr(JSContext*, JSValueConst obj, const char* str);
struct list_head* js_module_list(JSContext*);
int64_t js_array_length(JSContext*, JSValueConst array);
char** js_array_to_argv(JSContext*, int* argcp, JSValueConst array);
int js_offset_length(JSContext*, int64_t size, int argc, JSValueConst argv[], OffsetLength* off_len_p);
JSValue js_argv_to_array(JSContext*, const char* const* argv);
static inline BOOL
js_atom_is_int32(JSAtom atom) {
  if((int32_t)atom < 0)
    return TRUE;
  return FALSE;
}

static inline BOOL
js_atom_valid(JSAtom atom) {
  return atom != 0x7fffffff;
}

BOOL js_atom_is_index(JSContext*, int64_t* pval, JSAtom atom);
BOOL js_atom_compare_string(JSContext*, JSAtom atom, const char* other);
BOOL js_atom_is_length(JSContext*, JSAtom atom);
BOOL js_atom_is_symbol(JSContext*, JSAtom atom);
JSBuffer js_input_buffer(JSContext*, JSValueConst value);
JSBuffer js_input_chars(JSContext*, JSValueConst value);
JSBuffer js_input_args(JSContext*, int argc, JSValueConst argv[]);
JSBuffer js_input_argv(JSContext* ctx, int*, JSValueConst**);
int js_buffer_fromargs(JSContext*, int argc, JSValueConst argv[], JSBuffer* buf);
BOOL js_is_arraybuffer(JSContext*, JSValueConst value);
BOOL js_is_dataview(JSContext*, JSValueConst value);
BOOL js_is_typedarray(JSContext*, JSValueConst value);
JSValue js_typedarray_constructor(JSContext*, int bits, BOOL floating, BOOL sign);
JSValue js_typedarray_new(JSContext*, int bits, BOOL floating, BOOL sign, JSValueConst buffer, uint32_t byte_offset, uint32_t length);

static inline void
js_entry_init(JSEntry* entry) {
  entry->key = -1;
  entry->value = JS_UNDEFINED;
}

static inline void
js_entry_clear(JSContext* ctx, JSEntry* entry) {
  if(entry->key >= 0)
    JS_FreeAtom(ctx, entry->key);
  entry->key = -1;
  JS_FreeValue(ctx, entry->value);
  entry->value = JS_UNDEFINED;
}

static inline void
js_entry_clear_rt(JSRuntime* rt, JSEntry* entry) {
  if(entry->key >= 0)
    JS_FreeAtomRT(rt, entry->key);
  entry->key = -1;
  JS_FreeValueRT(rt, entry->value);
  entry->value = JS_UNDEFINED;
}

static inline void
js_entry_reset(JSContext* ctx, JSEntry* entry, JSAtom key, JSValue value) {
  js_entry_clear(ctx, entry);
  entry->key = key;
  entry->value = value;
}

static inline void
js_entry_reset_string(JSContext* ctx, JSEntry* entry, const char* keystr, JSValue value) {
  JSAtom key;
  key = JS_NewAtom(ctx, keystr);
  js_entry_reset(ctx, entry, key, value);
}

static inline const char*
js_entry_key_string(JSContext* ctx, const JSEntry entry) {
  const char* str;
  str = JS_AtomToCString(ctx, entry.key);
  return str;
}

static inline const char*
js_entry_value_string(JSContext* ctx, const JSEntry entry) {
  const char* str;
  str = JS_ToCString(ctx, entry.value);
  return str;
}

static inline JSEntry
js_entry_dup(JSContext* ctx, const JSEntry entry) {
  JSEntry ret = {JS_DupAtom(ctx, entry.key), JS_DupValue(ctx, entry.value)};
  return ret;
}

static inline int
js_entry_apply(JSContext* ctx, JSEntry* entry, JSValueConst obj) {
  int ret;
  ret = JS_SetProperty(ctx, obj, entry->key, entry->value);
  JS_FreeAtom(ctx, entry->key);
  entry->key = -1;
  entry->value = JS_UNDEFINED;
  return ret;
}

static inline void
ol_init(OffsetLength* ol) {
  ol->offset = 0;
  ol->length = INT64_MAX;
}

static inline BOOL
ol_is_default(const OffsetLength* ol) {
  return ol->offset == 0 && ol->length == INT64_MAX;
}

static inline uint8_t*
ol_data(const OffsetLength* ol, const void* x) {
  return (uint8_t*)x + ol->offset;
}

static inline size_t
ol_size(const OffsetLength* ol, int64_t n) {
  return MIN(ol->length, n - ol->offset);
}

/*static inline MemoryBlock
ol_block(const OffsetLength* ol, const void* x, size_t n) {
  return (MemoryBlock){ol_data(ol, x), ol_size(ol, n)};
}*/

/*static inline PointerRange
ol_range(const OffsetLength* ol, const void* x, size_t n) {
  MemoryBlock mb = offset_block(ol, x, n);
  return range_from(&mb);
}*/

static inline OffsetLength
ol_slice(const OffsetLength ol, int64_t start, int64_t end) {
  if(start < 0)
    start = ol.length + (start % ol.length);
  else if(start > ol.length)
    start = ol.length;
  if(end < 0)
    end = ol.length + (end % ol.length);
  else if(end > ol.length)
    end = ol.length;

  return (OffsetLength){start, end - start};
}

static inline OffsetLength
ol_offset(const OffsetLength* ol, const OffsetLength* by) {
  OffsetLength ret;
  ret.offset = ol->offset + by->offset;
  ret.length = MIN(by->length, ol->length - by->offset);
  return ret;
}

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

static inline const uint8_t*
js_buffer_begin(const JSBuffer* in) {
  return in->data;
}

static inline const uint8_t*
js_buffer_end(const JSBuffer* in) {
  return in->data + in->size;
}

JSValue js_typedarray_constructor(JSContext*, int bits, BOOL floating, BOOL sign);
JSValue js_typedarray_new(JSContext*, int bits, BOOL floating, BOOL sign, JSValueConst buffer, uint32_t byte_offset, uint32_t length);

typedef JSValue CClosureFunc(JSContext*, JSValueConst, int, JSValueConst[], int, void*);

JSValue js_function_cclosure(JSContext*, CClosureFunc* func, int length, int magic, void* opaque, void (*opaque_finalize)(void*));

#endif /* QJSNET_LIB_JS_UTILS_H */

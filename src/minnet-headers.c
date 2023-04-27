#include "minnet-headers.h"
#include "jsutils.h"
#include "utils.h"
#include "query.h"
#include <assert.h>
#include <limits.h>
#include <ctype.h>
#include <strings.h>

THREAD_LOCAL JSValue minnet_headers_proto;
THREAD_LOCAL JSClassID minnet_headers_class_id = 0;

enum {
  HEADERS_APPEND,
  HEADERS_DELETE,
  HEADERS_ENTRIES,
  HEADERS_VALUES,
  HEADERS_GET,
  HEADERS_HAS,
  HEADERS_KEYS,
  HEADERS_SET,

};
typedef void HeadersFreeFunc(void* opaque, JSRuntime* rt);
struct MinnetHeadersOpaque {
  ByteBuffer* headers;
  void* opaque;
  HeadersFreeFunc* free_func;
};

void*
minnet_headers_dup_obj(JSContext* ctx, JSValueConst obj) {
  if(JS_IsObject(obj)) {
    JS_DupValue(ctx, obj);
    return JS_VALUE_GET_OBJ(obj);
  }
  return 0;
}

void
minnet_headers_free_obj(void* obj, JSRuntime* rt) {
  JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_OBJECT, obj));
}

struct MinnetHeadersOpaque*
minnet_headers_opaque(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_headers_class_id);
}

ByteBuffer*
minnet_headers_data2(JSContext* ctx, JSValueConst obj) {
  struct MinnetHeadersOpaque* ptr;
  if(!(ptr = JS_GetOpaque2(ctx, obj, minnet_headers_class_id)))
    return 0;
  return ptr->headers;
}

JSValue
minnet_headers_value(JSContext* ctx, ByteBuffer* headers, JSValueConst obj) {
  JSValue headers_obj = JS_NewObjectProtoClass(ctx, minnet_headers_proto, minnet_headers_class_id);
  struct MinnetHeadersOpaque* ptr;

  if(!minnet_headers_class_id)
    minnet_headers_init(ctx, 0);

  if(!(ptr = js_malloc(ctx, sizeof(struct MinnetHeadersOpaque))))
    return JS_EXCEPTION;

  ptr->headers = headers;
  ptr->opaque = minnet_headers_dup_obj(ctx, obj);
  ptr->free_func = minnet_headers_free_obj;

  JS_SetOpaque(headers_obj, ptr);

  return headers_obj;
}

JSValue
minnet_headers_wrap(JSContext* ctx, ByteBuffer* headers, void* opaque, void (*free_func)(void* opaque, JSRuntime* rt)) {
  JSValue headers_obj = JS_NewObjectProtoClass(ctx, minnet_headers_proto, minnet_headers_class_id);
  struct MinnetHeadersOpaque* ptr;

  if(!minnet_headers_class_id)
    minnet_headers_init(ctx, 0);

  if(!(ptr = js_malloc(ctx, sizeof(struct MinnetHeadersOpaque))))
    return JS_EXCEPTION;

  ptr->headers = headers;
  ptr->opaque = opaque;
  ptr->free_func = free_func;

  JS_SetOpaque(headers_obj, ptr);

  return headers_obj;
}

static JSValue
minnet_headers_get(JSContext* ctx, JSValueConst this_val, int magic) {
  ByteBuffer* headers;
  JSValue ret = JS_UNDEFINED;

  if(!(headers = minnet_headers_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static JSValue
minnet_headers_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  ByteBuffer* headers;
  JSValue ret = JS_UNDEFINED;
  size_t len;
  const char* str;

  if(!(headers = minnet_headers_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {}
  return ret;
}

enum {
  HEADERS_TO_STRING,
  HEADERS_TO_OBJECT,
};

JSValue
minnet_headers_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  ByteBuffer* headers;
  JSValue ret = JS_UNDEFINED;

  if(!(headers = minnet_headers_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case HEADERS_APPEND: {
      size_t namelen, valuelen;
      const char* name = JS_ToCStringLen(ctx, &namelen, argv[0]);
      const char* value = JS_ToCStringLen(ctx, &valuelen, argv[1]);

      ret = JS_NewInt64(ctx, headers_appendb(headers, name, namelen, value, valuelen));
      JS_FreeCString(ctx, name);
      JS_FreeCString(ctx, value);
      break;
    }
    case HEADERS_DELETE: {
      size_t namelen;
      const char* name = JS_ToCStringLen(ctx, &namelen, argv[0]);

      ret = JS_NewInt64(ctx, headers_unsetb(headers, name, namelen));
      break;
    }
    case HEADERS_GET: {
      size_t valuelen;
      const char* name = JS_ToCString(ctx, argv[0]);
      const char* value;

      if((value = headers_getlen(headers, &valuelen, name)))
        ret = JS_NewStringLen(ctx, value, valuelen);
      break;
    }
    case HEADERS_HAS: {
      size_t namelen;
      const char* name = JS_ToCStringLen(ctx, &namelen, argv[0]);

      ret = JS_NewBool(ctx, headers_findb(headers, name, namelen) != -1);
      break;
    }

    case HEADERS_SET: {
      size_t valuelen;
      const char* name = JS_ToCString(ctx, argv[0]);
      const char* value = JS_ToCString(ctx, argv[1]);
      ret = JS_NewInt64(ctx, headers_set(headers, name, value));

      break;
    }
  }
  return ret;
}

JSValue
minnet_headers_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  ByteBuffer* headers;
  JSValue ret = JS_UNDEFINED;

  if(!(headers = minnet_headers_data2(ctx, this_val)))
    return JS_EXCEPTION;

  ret = JS_NewArray(ctx);
  uint32_t i = 0;
  uint8_t *ptr, *end = headers->write;

  for(ptr = headers->start; ptr != end; ptr += headers_next(ptr, end)) {
    size_t len;
    JSValue name = JS_NULL, value = JS_NULL, entry = JS_NULL;

    if(magic == HEADERS_KEYS || magic == HEADERS_ENTRIES) {
      len = headers_namelen(ptr, end);
      name = JS_NewStringLen(ctx, ptr, len);
    }

    if(magic == HEADERS_VALUES || magic == HEADERS_ENTRIES) {
      ptr += headers_value(ptr, end);
      len = headers_length(ptr, end);
      value = JS_NewStringLen(ctx, ptr, len);
    }

    if(magic == HEADERS_ENTRIES) {
      entry = JS_NewArray(ctx);
      JS_SetPropertyUint32(ctx, entry, 0, name);
      JS_SetPropertyUint32(ctx, entry, 0, value);
    } else {
      entry = magic == HEADERS_KEYS ? name : value;
      JS_FreeValue(ctx, magic == HEADERS_KEYS ? value : name);
    }

    JS_SetPropertyUint32(ctx, ret, i++, entry);
  }

  return ret;
}

JSValue
minnet_headers_from(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  ByteBuffer* headers;
  JSValue ret = JS_NULL;

  if(!(headers = js_mallocz(ctx, sizeof(ByteBuffer))))
    return JS_EXCEPTION;

  /* headers_fromvalue(headers, argv[0], ctx);

   if(!headers_valid(*headers))
     return JS_ThrowTypeError(ctx, "Not a valid Headers");

   return minnet_headers_wrap(ctx, headers);*/
  return ret;
}

JSValue
minnet_headers_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret = JS_NULL;

  return ret;
}

static const JSCFunctionListEntry minnet_headers_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("delete", 1, minnet_headers_method, HEADERS_DELETE),
    JS_CFUNC_MAGIC_DEF("keys", 0, minnet_headers_iterator, HEADERS_KEYS),
    JS_CFUNC_MAGIC_DEF("values", 0, minnet_headers_iterator, HEADERS_VALUES),
    JS_CFUNC_MAGIC_DEF("entries", 0, minnet_headers_iterator, HEADERS_ENTRIES),
    JS_CFUNC_MAGIC_DEF("append", 1, minnet_headers_method, HEADERS_APPEND),
    JS_CFUNC_MAGIC_DEF("get", 1, minnet_headers_method, HEADERS_GET),
    JS_CFUNC_MAGIC_DEF("has", 1, minnet_headers_method, HEADERS_HAS),
    JS_CFUNC_MAGIC_DEF("set", 2, minnet_headers_method, HEADERS_SET),
    JS_CFUNC_DEF("inspect", 0, minnet_headers_inspect),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetHeaders", JS_PROP_CONFIGURABLE),
};

static int
minnet_headers_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {
  ByteBuffer* headers = minnet_headers_data2(ctx, obj);
  const char* propstr = JS_AtomToCString(ctx, prop);
  char* value;
  size_t len;
  BOOL ret = FALSE;

  if((value = headers_getlen(headers, &len, propstr))) {
    if(pdesc) {
      pdesc->flags = JS_PROP_ENUMERABLE;
      pdesc->value = JS_NewStringLen(ctx, value, len);
      pdesc->getter = JS_UNDEFINED;
      pdesc->setter = JS_UNDEFINED;
    }
    ret = TRUE;
  }
  JS_FreeCString(ctx, propstr);

  return ret;
}

static int
minnet_headers_get_own_property_names(JSContext* ctx, JSPropertyEnum** ptab, uint32_t* plen, JSValueConst obj) {
  ByteBuffer* headers = minnet_headers_data2(ctx, obj);
  uint32_t i = 0, size = headers_size(headers);
  uint8_t *ptr, *end = headers->write;
  JSPropertyEnum* props = js_malloc(ctx, sizeof(JSPropertyEnum) * size);

  for(ptr = headers->start; i < size && ptr != end; ptr += headers_next(ptr, end)) {
    size_t len = headers_namelen(ptr, end);
    JSAtom prop = JS_NewAtomLen(ctx, ptr, len);

    props[i].is_enumerable = TRUE;
    props[i].atom = prop;

    ++i;
  }
  *ptab = props;
  *plen = size;

  return 0;
}

static int
minnet_headers_has_property(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  ByteBuffer* headers = minnet_headers_data2(ctx, obj);
  const char* propstr = JS_AtomToCString(ctx, prop);
  ssize_t index;
  BOOL ret = FALSE;

  if((index = headers_find(headers, propstr)) != -1)
    ret = TRUE;

  JS_FreeCString(ctx, propstr);
  return ret;
}

static JSValue
minnet_headers_get_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst receiver) {
  ByteBuffer* headers = minnet_headers_data2(ctx, obj);
  JSValue value = JS_UNDEFINED;
  const char* propstr = JS_AtomToCString(ctx, prop);

  return value;
}

static int
minnet_headers_set_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst value, JSValueConst receiver, int flags) {
  ByteBuffer* headers = minnet_headers_data2(ctx, obj);
  const char* propstr = JS_AtomToCString(ctx, prop);
  const char* valuestr = JS_ToCString(ctx, value);

  if(1) {
    headers_set(headers, propstr, valuestr);
    return TRUE;
  }

  return FALSE;
}

static void
minnet_headers_finalizer(JSRuntime* rt, JSValue val) {
  struct MinnetHeadersOpaque* ptr;

  if((ptr = minnet_headers_opaque(val))) {

    ptr->free_func(ptr->opaque, rt);

    // buffer_free(headers);
    js_free_rt(rt, ptr);
  }
}

static JSClassExoticMethods minnet_headers_exotic_methods = {
    .get_own_property = minnet_headers_get_own_property,
    .get_own_property_names = minnet_headers_get_own_property_names,
    .has_property = minnet_headers_has_property,
    //   .get_property = minnet_headers_get_property,
    .set_property = minnet_headers_set_property,
};

static JSClassDef minnet_headers_class = {
    .class_name = "MinnetHeaders",
    .finalizer = minnet_headers_finalizer,
    .exotic = &minnet_headers_exotic_methods,
};

int
minnet_headers_init(JSContext* ctx, JSModuleDef* m) {
  JSAtom inspect_atom;

  // Add class Headers
  JS_NewClassID(&minnet_headers_class_id);
  JS_NewClass(JS_GetRuntime(ctx), minnet_headers_class_id, &minnet_headers_class);
  minnet_headers_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_headers_proto, minnet_headers_proto_funcs, countof(minnet_headers_proto_funcs));

  /* inspect_atom = js_symbol_static_atom(ctx, "inspect");

   if(!js_atom_is_symbol(ctx, inspect_atom)) {
     JS_FreeAtom(ctx, inspect_atom);
     inspect_atom = js_symbol_for_atom(ctx, "quickjs.inspect.custom");
   }

   if(js_atom_is_symbol(ctx, inspect_atom))
     JS_SetProperty(ctx, minnet_headers_proto, inspect_atom, JS_NewCFunction(ctx, minnet_headers_inspect, "inspect", 0));

   JS_FreeAtom(ctx, inspect_atom);*/

  /*  if(m)
      JS_SetModuleExport(ctx, m, "Headers", minnet_headers_ctor);
  */
  return 0;
}

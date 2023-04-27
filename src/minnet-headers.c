#include "minnet-headers.h"
#include "jsutils.h"
#include "utils.h"
#include "query.h"
#include <assert.h>
#include <limits.h>
#include <ctype.h>
#include <strings.h>

THREAD_LOCAL JSValue minnet_headers_proto;
THREAD_LOCAL JSClassID minnet_headers_class_id;

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

ByteBuffer*
minnet_headers_data(JSValueConst obj) {
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

  if(!(ptr = js_malloc(ctx, sizeof(struct MinnetHeadersOpaque))))
    return JS_EXCEPTION;

  ptr->headers = headers;
  ptr->opaque = opaque;
  ptr->free_func = free_func;

  JS_SetOpaque(headers_obj, ptr);

  return headers_obj;
}

static void
minnet_headers_free(void* opaque, JSRuntime* rt) {
  buffer_free(opaque);
  js_free_rt(rt, opaque);
}

JSValue
minnet_headers_new(JSContext* ctx, ByteBuffer* b) {
  ByteBuffer* headers;

  if(!(headers = js_malloc(ctx, sizeof(ByteBuffer))))
    return JS_EXCEPTION;

  *headers = buffer_move(b);

  return minnet_headers_wrap(ctx, headers, headers, (HeadersFreeFunc*)&buffer_free);
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
  JSValue value = JS_UNDEFINED;
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    /*    if(index < 0)
          index = ((index % headers->n) + headers->n) % headers->n;

        if(index < (int64_t)headers->n) {
          JSAtom key = headers->atoms[index];
          value = (key & (1U << 31)) ? JS_NewUint32(ctx, key & (~(1U << 31))) : JS_AtomToValue(ctx, key);

          if(pdesc) {
            pdesc->flags = JS_PROP_ENUMERABLE;
            pdesc->value = value;
            pdesc->getter = JS_UNDEFINED;
            pdesc->setter = JS_UNDEFINED;
          }
          return TRUE;
        }*/
  }
  return FALSE;
}

static int
minnet_headers_get_own_property_names(JSContext* ctx, JSPropertyEnum** ptab, uint32_t* plen, JSValueConst obj) {
  ByteBuffer* headers;
  uint32_t i, len;
  JSPropertyEnum* props;

  if((headers = minnet_headers_data2(ctx, obj)))
    len = buffer_SIZE(headers);
  else {
    JSValue length = JS_GetPropertyStr(ctx, obj, "length");
    JS_ToUint32(ctx, &len, length);
    JS_FreeValue(ctx, length);
  }

  props = js_malloc(ctx, sizeof(JSPropertyEnum) * (len + 1));

  for(i = 0; i < len; i++) {
    props[i].is_enumerable = TRUE;
    props[i].atom = i | (1U << 31);
  }

  props[len].is_enumerable = TRUE;
  props[len].atom = JS_NewAtom(ctx, "length");

  *ptab = props;
  *plen = len + 1;
  return 0;
}

static int
minnet_headers_has_property(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  ByteBuffer* headers = minnet_headers_data2(ctx, obj);
  int64_t index;

  /*  if(js_atom_is_index(ctx, &index, prop)) {
      if(index < 0)
        index = ((index % (int64_t)(headers->n + 1)) + headers->n);

      if(index < (int64_t)headers->n)
        return TRUE;
    } else if(js_atom_is_length(ctx, prop)) {
      return TRUE;
    } else {
      JSValue proto = JS_GetPrototype(ctx, obj);
      if(JS_IsObject(proto) && JS_HasProperty(ctx, proto, prop))
        return TRUE;
    }*/

  return FALSE;
}

static JSValue
minnet_headers_get_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst receiver) {
  ByteBuffer* headers = minnet_headers_data2(ctx, obj);
  JSValue value = JS_UNDEFINED;
  int64_t index;
  int32_t entry;

  /*  if(js_atom_is_index(ctx, &index, prop)) {
      if(index < 0)
        index = ((index % (int64_t)(headers->n + 1)) + headers->n);

      if(index < (int64_t)headers->n) {
        JSAtom key = headers->atoms[index];
        value = (key & (1U << 31)) ? JS_NewUint32(ctx, key & (~(1U << 31))) : JS_AtomToValue(ctx, key);
      }
    } else if(js_atom_is_length(ctx, prop)) {
      value = JS_NewUint32(ctx, headers->n);
    } else if((entry = js_find_cfunction_atom(ctx, minnet_headers_proto_funcs, countof(minnet_headers_proto_funcs), prop, JS_DEF_CGETSET_MAGIC)) >= 0) {

      // printf("entry: %d magic: %d\n", entry,
      // minnet_headers_proto_funcs[entry].magic);
      value = minnet_headers_get(ctx, obj, minnet_headers_proto_funcs[entry].magic);

    } else {
      JSValue proto = JS_IsUndefined(headers_proto) ? JS_GetPrototype(ctx, obj) : headers_proto;
      if(JS_IsObject(proto))
        value = JS_GetProperty(ctx, proto, prop);
    }
  */
  return value;
}

static int
minnet_headers_set_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst value, JSValueConst receiver, int flags) {
  ByteBuffer* headers = minnet_headers_data2(ctx, obj);
  int64_t index;

  /*if(js_atom_is_index(ctx, &index, prop)) {
    if(index < 0)
      index = ((index % (int64_t)(headers->n + 1)) + headers->n);

    if(index == (int64_t)headers->n)
      headers_push(headers, ctx, value);
    else if(index < (int64_t)headers->n)
      headers->atoms[index] = JS_ValueToAtom(ctx, value);
    return TRUE;
  }
*/
  return FALSE;
}

static void
minnet_headers_finalizer(JSRuntime* rt, JSValue val) {
  ByteBuffer* headers = JS_GetOpaque(val, minnet_headers_class_id);
  if(headers) {
    buffer_free(headers);
    js_free_rt(rt, headers);
  }
}

static JSClassExoticMethods minnet_headers_exotic_methods = {
    .get_own_property = minnet_headers_get_own_property,
    .get_own_property_names = minnet_headers_get_own_property_names,
    .has_property = minnet_headers_has_property,
    .get_property = minnet_headers_get_property,
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

  // minnet_headers_ctor = JS_NewObject(ctx);
  // JS_SetPropertyFunctionList(ctx, minnet_headers_ctor, minnet_headers_static_funcs, countof(minnet_headers_static_funcs));

  // JS_SetConstructor(ctx, minnet_headers_ctor, minnet_headers_proto);

  inspect_atom = js_symbol_static_atom(ctx, "inspect");

  if(!js_atom_is_symbol(ctx, inspect_atom)) {
    JS_FreeAtom(ctx, inspect_atom);
    inspect_atom = js_symbol_for_atom(ctx, "quickjs.inspect.custom");
  }

  if(js_atom_is_symbol(ctx, inspect_atom))
    JS_SetProperty(ctx, minnet_headers_proto, inspect_atom, JS_NewCFunction(ctx, minnet_headers_inspect, "inspect", 0));

  JS_FreeAtom(ctx, inspect_atom);

  /*  if(m)
      JS_SetModuleExport(ctx, m, "Headers", minnet_headers_ctor);
  */
  return 0;
}

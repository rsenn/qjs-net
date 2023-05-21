#define _GNU_SOURCE
#include <quickjs.h>
#include <cutils.h>
#include "minnet-form-parser.h"
#include "callback.h"
#include "jsutils.h"
#include <ctype.h>
#include <strings.h>
#include <libwebsockets.h>

THREAD_LOCAL JSClassID minnet_form_parser_class_id;
THREAD_LOCAL JSValue minnet_form_parser_proto, minnet_form_parser_ctor;

enum { FORM_PARSER_PARAMS, FORM_PARSER_SOCKET, FORM_PARSER_READ, FORM_PARSER_ON_OPEN, FORM_PARSER_ON_CONTENT, FORM_PARSER_ON_CLOSE, FORM_PARSER_ON_FINALIZE };

JSValue
minnet_form_parser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetFormParser* fp;
  MinnetWebsocket* ws;
  char** param_names;
  int param_count;
  uint64_t chunk_size = 1024;

  if(!(fp = form_parser_alloc(ctx)))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, minnet_form_parser_proto);

  obj = JS_NewObjectProtoClass(ctx, proto, minnet_form_parser_class_id);
  JS_FreeValue(ctx, proto);
  if(JS_IsException(obj))
    goto fail;

  if(argc < 1 || (ws = minnet_ws_data(argv[0])) == 0) {
    JS_ThrowInternalError(ctx, "argument 1 must be Socket");
    goto fail;
  }
  if(argc < 2 || (param_names = js_array_to_argv(ctx, &param_count, argv[1])) == 0) {
    JS_ThrowInternalError(ctx, "argument 2 must be Array of parameter names");
    goto fail;
  }

  JS_SetOpaque(obj, fp);

  if(argc >= 3 && JS_IsObject(argv[2])) {
    JSValue this_val = obj;

    JSValue cb_content = JS_GetPropertyStr(ctx, argv[2], "onContent");
    JSValue cb_open = JS_GetPropertyStr(ctx, argv[2], "onOpen");
    JSValue cb_close = JS_GetPropertyStr(ctx, argv[2], "onClose");
    JSValue cb_finalize = JS_GetPropertyStr(ctx, argv[2], "onFinalize");
    JSValue opt_chunksz = JS_GetPropertyStr(ctx, argv[2], "chunkSize");

    GETCB(cb_content, fp->cb.content)
    GETCB(cb_open, fp->cb.open)
    GETCB(cb_close, fp->cb.close)
    GETCB(cb_finalize, fp->cb.finalize)

    JS_SetPropertyStr(ctx, obj, "oncontent", fp->cb.content.func_obj);
    JS_SetPropertyStr(ctx, obj, "onopen", fp->cb.open.func_obj);
    JS_SetPropertyStr(ctx, obj, "onclose", fp->cb.close.func_obj);
    JS_SetPropertyStr(ctx, obj, "onfinalize", fp->cb.finalize.func_obj);

    if(JS_IsNumber(opt_chunksz)) {
      JS_ToIndex(ctx, &chunk_size, opt_chunksz);
    }
  }

  form_parser_init(fp, ws, param_count, (const char* const*)param_names, chunk_size);

  {
    struct wsi_opaque_user_data* opaque = ws_opaque(ws);

    if(opaque->form_parser)
      form_parser_free(opaque->form_parser, ctx);

    opaque->form_parser = fp;
  }

  return obj;

fail:
  js_free(ctx, fp);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
minnet_form_parser_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetFormParser* fp;
  JSValue ret = JS_UNDEFINED;

  if(!(fp = minnet_form_parser_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {

    case FORM_PARSER_PARAMS: {
      ret = js_argv_to_array(ctx, fp->spa_create_info.param_names);
      break;
    }
    case FORM_PARSER_SOCKET: {
      ret = minnet_ws_wrap(ctx, fp->ws);
      break;
    }
    case FORM_PARSER_READ: {
      ret = JS_NewUint32(ctx, fp->read);
      break;
    }
    case FORM_PARSER_ON_OPEN: {
      ret = JS_DupValue(ctx, fp->cb.open.func_obj);
      break;
    }
    case FORM_PARSER_ON_CONTENT: {
      ret = JS_DupValue(ctx, fp->cb.content.func_obj);
      break;
    }
    case FORM_PARSER_ON_CLOSE: {
      ret = JS_DupValue(ctx, fp->cb.close.func_obj);
      break;
    }
    case FORM_PARSER_ON_FINALIZE: {
      ret = JS_DupValue(ctx, fp->cb.finalize.func_obj);
      break;
    }
  }
  return ret;
}

static JSValue
minnet_form_parser_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetFormParser* fp;
  JSValue ret = JS_UNDEFINED;

  if(!(fp = minnet_form_parser_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {

    case FORM_PARSER_ON_OPEN: {
      JS_FreeValue(ctx, fp->cb.open.func_obj);
      JS_FreeValue(ctx, fp->cb.open.this_obj);
      fp->cb.open.ctx = ctx;
      fp->cb.open.func_obj = JS_DupValue(ctx, value);
      fp->cb.open.this_obj = JS_DupValue(ctx, this_val);
      break;
    }
    case FORM_PARSER_ON_CONTENT: {
      JS_FreeValue(ctx, fp->cb.content.func_obj);
      JS_FreeValue(ctx, fp->cb.content.this_obj);
      fp->cb.content.ctx = ctx;
      fp->cb.content.func_obj = JS_DupValue(ctx, value);
      fp->cb.content.this_obj = JS_DupValue(ctx, this_val);
      break;
    }
    case FORM_PARSER_ON_CLOSE: {
      JS_FreeValue(ctx, fp->cb.close.func_obj);
      JS_FreeValue(ctx, fp->cb.close.this_obj);
      fp->cb.close.ctx = ctx;
      fp->cb.close.func_obj = JS_DupValue(ctx, value);
      fp->cb.close.this_obj = JS_DupValue(ctx, this_val);
      break;
    }
    case FORM_PARSER_ON_FINALIZE: {
      JS_FreeValue(ctx, fp->cb.finalize.func_obj);
      JS_FreeValue(ctx, fp->cb.finalize.this_obj);
      fp->cb.finalize.ctx = ctx;
      fp->cb.finalize.func_obj = JS_DupValue(ctx, value);
      fp->cb.finalize.this_obj = JS_DupValue(ctx, this_val);
      break;
    }
  }

  return ret;
}

static void
minnet_form_parser_finalizer(JSRuntime* rt, JSValue val) {
  MinnetFormParser* fp;

  if((fp = minnet_form_parser_data(val)))
    form_parser_free_rt(fp, rt);
}

static int
minnet_form_parser_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {
  MinnetFormParser* fp = minnet_form_parser_data2(ctx, obj);
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(form_parser_param_valid(fp, index)) {

      pdesc->flags = JS_PROP_ENUMERABLE;
      pdesc->value = JS_NewString(ctx, form_parser_param_name(fp, index));
      pdesc->getter = JS_UNDEFINED;
      pdesc->setter = JS_UNDEFINED;
      return TRUE;
    }
  } else if(js_atom_is_length(ctx, prop)) {
    pdesc->flags = JS_PROP_ENUMERABLE;
    pdesc->value = JS_NewUint32(ctx, form_parser_param_count(fp));
    pdesc->getter = JS_UNDEFINED;
    pdesc->setter = JS_UNDEFINED;
    return TRUE;
  } else {
    const char* str = JS_AtomToCString(ctx, prop);
    BOOL ret = FALSE;
    int index;

    if(strncmp(str, "on", 2)) {

      if((index = form_parser_param_index(fp, str)) != -1) {
        ret = TRUE;
        pdesc->flags = JS_PROP_ENUMERABLE;
        pdesc->value = JS_NewStringLen(ctx, lws_spa_get_string(fp->spa, index), lws_spa_get_length(fp->spa, index));
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
      }
    }

    JS_FreeCString(ctx, str);
    return ret;
  }

  return FALSE;
}

static int
minnet_form_parser_get_own_property_names(JSContext* ctx, JSPropertyEnum** ptab, uint32_t* plen, JSValueConst obj) {
  MinnetFormParser* fp = minnet_form_parser_data2(ctx, obj);
  JSPropertyEnum* props;
  size_t i, len = form_parser_param_count(fp);

  props = js_malloc(ctx, sizeof(JSPropertyEnum) * (len + 1));

  for(i = 0; i < len; i++) {
    props[i].is_enumerable = TRUE;
    props[i].atom = JS_NewAtom(ctx, form_parser_param_name(fp, i));
  }

  props[len].is_enumerable = TRUE;
  props[len].atom = JS_NewAtom(ctx, "length");

  *ptab = props;
  *plen = len + 1;
  return 0;
}

static int
minnet_form_parser_has_property(JSContext* ctx, JSValueConst obj, JSAtom prop) {
  MinnetFormParser* fp = minnet_form_parser_data2(ctx, obj);
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {

    return form_parser_param_valid(fp, index);
  } else if(js_atom_is_length(ctx, prop)) {
    return TRUE;
  } else {
    const char* str = JS_AtomToCString(ctx, prop);

    BOOL ret = form_parser_param_exists(fp, str);

    JS_FreeCString(ctx, str);
    return ret;
  }

  return FALSE;
}

static JSValue
minnet_form_parser_get_property(JSContext* ctx, JSValueConst obj, JSAtom prop, JSValueConst receiver) {
  MinnetFormParser* fp = minnet_form_parser_data2(ctx, obj);
  JSValue value = JS_UNDEFINED;
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    if(form_parser_param_valid(fp, index)) {

      value = JS_NewString(ctx, form_parser_param_name(fp, index));
    }
  } else if(js_atom_is_length(ctx, prop)) {
    value = JS_NewUint32(ctx, form_parser_param_count(fp));
  } else {
    const char* str = JS_AtomToCString(ctx, prop);

    int index;

    if((index = form_parser_param_index(fp, str)) != -1) {
      value = JS_NewStringLen(ctx, lws_spa_get_string(fp->spa, index), lws_spa_get_length(fp->spa, index));

    } else {
      JSValue proto = JS_GetPrototype(ctx, obj);
      if(JS_IsObject(proto) && JS_HasProperty(ctx, proto, prop))
        value = JS_GetProperty(ctx, proto, prop);
    }

    JS_FreeCString(ctx, str);
  }

  return value;
}

static int
minnet_form_parser_define_own_property(JSContext* ctx, JSValueConst this_obj, JSAtom prop, JSValueConst val, JSValueConst getter, JSValueConst setter, int flags) {
  // MinnetFormParser* fp = minnet_form_parser_data2(ctx, this_obj);

  if(js_atom_is_index(ctx, NULL, prop)) {
    return TRUE;
  } else if(js_atom_is_length(ctx, prop)) {
    return TRUE;
  }

  /* run the default define own property */
  return JS_DefineProperty(ctx, this_obj, prop, val, getter, setter, flags | JS_PROP_NO_EXOTIC);
}

static JSValue
minnet_form_parser_call(JSContext* ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst argv[], int flags) {
  MinnetFormParser* fp = minnet_form_parser_data2(ctx, func_obj);
  JSValue ret = JS_UNDEFINED;

  if(argc < 1)
    return JS_ThrowInternalError(ctx, "argument 1 must be String, ArrayBuffer or null");

  if(JS_IsNull(argv[0])) {
    ret = JS_NewInt32(ctx, lws_spa_finalize(fp->spa));

  } else {
    JSBuffer buf;

    js_buffer_from(ctx, &buf, argv[0]);

    if(buf.data == 0)
      return JS_ThrowInternalError(ctx, "argument 1 must be String or ArrayBuffer");

    ret = JS_NewInt32(ctx, form_parser_process(fp, buf.data, buf.size));

    js_buffer_free(&buf, ctx);
  }
  if(!JS_IsNull(fp->exception)) {

    JS_Throw(ctx, fp->exception);
    ret = JS_EXCEPTION;
  }
  return ret;
}

static JSClassExoticMethods minnet_form_parser_exotic_methods = {
    .get_own_property = minnet_form_parser_get_own_property,
    .get_own_property_names = minnet_form_parser_get_own_property_names,
    .has_property = minnet_form_parser_has_property,
    .define_own_property = minnet_form_parser_define_own_property,
    .get_property = minnet_form_parser_get_property,
    //.set_property = minnet_form_parser_set_property,
};
JSClassDef minnet_form_parser_class = {
    "MinnetFormParser",
    .finalizer = minnet_form_parser_finalizer,
    .exotic = &minnet_form_parser_exotic_methods,
    .call = &minnet_form_parser_call,
};

const JSCFunctionListEntry minnet_form_parser_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("socket", minnet_form_parser_get, minnet_form_parser_set, FORM_PARSER_SOCKET, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("params", minnet_form_parser_get, minnet_form_parser_set, FORM_PARSER_PARAMS, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("read", minnet_form_parser_get, 0, FORM_PARSER_READ, 0),
    /* JS_CGETSET_MAGIC_FLAGS_DEF("onclose", minnet_form_parser_get, 0, FORM_PARSER_ON_CLOSE, 0),
     JS_CGETSET_MAGIC_FLAGS_DEF("onopen", minnet_form_parser_get, 0, FORM_PARSER_ON_OPEN , 0),
     JS_CGETSET_MAGIC_FLAGS_DEF("oncontent", minnet_form_parser_get, 0, FORM_PARSER_ON_CONTENT , 0),
     JS_CGETSET_MAGIC_FLAGS_DEF("onfinalize", minnet_form_parser_get, 0, FORM_PARSER_ON_FINALIZE , 0),*/
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetFormParser", JS_PROP_CONFIGURABLE),
};

int
minnet_form_parser_init(JSContext* ctx, JSModuleDef* m) {
  // Add class FormParser
  JS_NewClassID(&minnet_form_parser_class_id);

  JS_NewClass(JS_GetRuntime(ctx), minnet_form_parser_class_id, &minnet_form_parser_class);
  minnet_form_parser_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_form_parser_proto, minnet_form_parser_proto_funcs, countof(minnet_form_parser_proto_funcs));
  JS_SetClassProto(ctx, minnet_form_parser_class_id, minnet_form_parser_proto);

  minnet_form_parser_ctor = JS_NewCFunction2(ctx, minnet_form_parser_constructor, "MinnetFormParser", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, minnet_form_parser_ctor, minnet_form_parser_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "FormParser", minnet_form_parser_ctor);

  return 0;
}

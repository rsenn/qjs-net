#define _GNU_SOURCE
#include <quickjs.h>
#include <cutils.h>
#include "minnet-form-parser.h"
#include "jsutils.h"
#include <ctype.h>
#include <strings.h>
#include <libwebsockets.h>

THREAD_LOCAL JSClassID minnet_form_parser_class_id;
THREAD_LOCAL JSValue minnet_form_parser_proto, minnet_form_parser_ctor;

enum { FORM_PARSER_PARAMS, FORM_PARSER_SOCKET, FORM_PARSER_ON_OPEN, FORM_PARSER_ON_CONTENT, FORM_PARSER_ON_CLOSE };

static int
form_parser_callback(void* data, const char* name, const char* filename, char* buf, int len, enum lws_spa_fileupload_states state) {
  MinnetFormParser* fp = data;
  MinnetCallback* cb = 0;
  JSValue args[2] = {JS_NULL, JS_NULL};

  switch(state) {
    case LWS_UFS_CONTENT:
    case LWS_UFS_FINAL_CONTENT: {
      cb = &fp->cb.content;
      if(cb->ctx && len > 0)
        args[1] = JS_NewArrayBufferCopy(cb->ctx, (uint8_t*)buf, len);
      break;
    }
    case LWS_UFS_OPEN: {
      cb = &fp->cb.open;
      if(cb->ctx && filename)
        args[1] = JS_NewString(cb->ctx, filename);
      break;
    }
    case LWS_UFS_CLOSE: {
      cb = &fp->cb.close;
      break;
    }
  }

  if(cb && cb->ctx) {
    JSValue ret;

    if(name)
      args[0] = JS_NewString(cb->ctx, name);

    ret = minnet_emit(cb, 2, args);

    if(JS_IsException(ret))
      js_error_print(cb->ctx, fp->exception = JS_GetException(cb->ctx));

    JS_FreeValue(cb->ctx, args[0]);
    JS_FreeValue(cb->ctx, args[1]);
  }

  return 0;
}

void
form_parser_init(MinnetFormParser* fp, MinnetWebsocket* ws, int nparams, const char* const* param_names, size_t chunk_size) {
  MinnetSession* session;

  memset(&fp->spa_create_info, 0, sizeof(struct lws_spa_create_info));
  fp->ws = ws_dup(ws);
  fp->spa_create_info.count_params = nparams;
  fp->spa_create_info.param_names = param_names;
  fp->spa_create_info.max_storage = chunk_size + 1;
  fp->spa_create_info.opt_cb = &form_parser_callback;
  fp->spa_create_info.opt_data = fp;

  fp->spa = lws_spa_create_via_info(ws->lwsi, &fp->spa_create_info);
  fp->exception = JS_NULL;
}

MinnetFormParser*
form_parser_alloc(JSContext* ctx) {
  MinnetFormParser* ret;

  ret = js_mallocz(ctx, sizeof(MinnetFormParser));
  ret->ref_count = 1;
  return ret;
}

MinnetFormParser*
form_parser_new(JSContext* ctx, MinnetWebsocket* ws, int nparams, const char* const* param_names, size_t chunk_size) {
  MinnetFormParser* fp;

  if((fp = form_parser_alloc(ctx)))
    form_parser_init(fp, ws, nparams, param_names, chunk_size);

  return fp;
}

MinnetFormParser*
form_parser_dup(MinnetFormParser* fp) {
  ++fp->ref_count;
  return fp;
}

void
form_parser_zero(MinnetFormParser* fp) {
  fp->ws = 0;
  fp->spa = 0;
  fp->lwsac_head = 0;
  memset(&fp->spa_create_info, 0, sizeof(struct lws_spa_create_info));
}

void
form_parser_clear(MinnetFormParser* fp, JSContext* ctx) {

  if(fp->spa) {
    lws_spa_destroy(fp->spa);
    fp->spa = 0;
  }

  if(fp->spa_create_info.param_names) {
    js_free(ctx, (void*)fp->spa_create_info.param_names);
  }
  memset(&fp->spa_create_info, 0, sizeof(struct lws_spa_create_info));

  FREECB(fp->cb.content);
  FREECB(fp->cb.open);
  FREECB(fp->cb.close);
}

void
form_parser_clear_rt(MinnetFormParser* fp, JSRuntime* rt) {

  if(fp->spa) {
    lws_spa_destroy(fp->spa);
    fp->spa = 0;
  }

  if(fp->spa_create_info.param_names) {
    js_free_rt(rt, (void*)fp->spa_create_info.param_names);
  }
  memset(&fp->spa_create_info, 0, sizeof(struct lws_spa_create_info));

  FREECB_RT(fp->cb.content);
  FREECB_RT(fp->cb.open);
  FREECB_RT(fp->cb.close);
}

void
form_parser_free(MinnetFormParser* fp, JSContext* ctx) {
  if(--fp->ref_count == 0) {
    ws_free(fp->ws, ctx);
    form_parser_clear(fp, ctx);
    js_free(ctx, fp);
  }
}

void
form_parser_free_rt(MinnetFormParser* fp, JSRuntime* rt) {
  if(--fp->ref_count == 0) {
    ws_free_rt(fp->ws, rt);
    form_parser_clear_rt(fp, rt);
    js_free_rt(rt, fp);
  }
}

char*
form_parser_param_name(MinnetFormParser* fp, int index) {
  if(index >= 0 && index < fp->spa_create_info.count_params) {
    return fp->spa_create_info.param_names[index];
  }
  return 0;
}

BOOL
form_parser_param_valid(MinnetFormParser* fp, int index) {
  if(index >= 0 && index < fp->spa_create_info.count_params) {
    return TRUE;
  }
  return FALSE;
}

size_t
form_parser_param_count(MinnetFormParser* fp) {
  return fp->spa_create_info.count_params;
}

int
form_parser_param_index(MinnetFormParser* fp, const char* name) {
  int i;
  for(i = 0; i < fp->spa_create_info.count_params; i++) {
    if(!strcmp(fp->spa_create_info.param_names[i], name))
      return i;
  }
  return -1;
}

BOOL
form_parser_param_exists(MinnetFormParser* fp, const char* name) {
  int i = form_parser_param_index(fp, name);

  return i != -1;
}

int
form_parser_process(MinnetFormParser* fp, const void* data, size_t len) {
  int retval = lws_spa_process(fp->spa, data, len);

  return retval;
}

JSValue
minnet_form_parser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetFormParser* fp;
  MinnetWebsocket* ws;
  char** param_names;
  int param_count;
  BOOL got_url = FALSE;
  uint64_t chunk_size = 1024;

  if(!(fp = form_parser_alloc(ctx)))
    return JS_ThrowOutOfMemory(ctx);

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

  if(argc >= 3 && JS_IsObject(argv[2])) {
    JSValue this_val = obj;

    JSValue cb_content = JS_GetPropertyStr(ctx, argv[2], "onContent");
    JSValue cb_open = JS_GetPropertyStr(ctx, argv[2], "onOpen");
    JSValue cb_close = JS_GetPropertyStr(ctx, argv[2], "onClose");
    JSValue opt_chunksz = JS_GetPropertyStr(ctx, argv[2], "chunkSize");

    GETCB(cb_content, fp->cb.content)
    GETCB(cb_open, fp->cb.open)
    GETCB(cb_close, fp->cb.close)

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

  JS_SetOpaque(obj, fp);

  return obj;

fail:
  js_free(ctx, fp);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
minnet_form_parser_new(JSContext* ctx, MinnetWebsocket* ws, int nparams, const char* const* param_names, size_t chunk_size) {
  MinnetFormParser* fp;

  if(!(fp = form_parser_new(ctx, ws, nparams, param_names, chunk_size)))
    return JS_ThrowOutOfMemory(ctx);

  return minnet_form_parser_wrap(ctx, fp);
}

JSValue
minnet_form_parser_wrap(JSContext* ctx, MinnetFormParser* fp) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_form_parser_proto, minnet_form_parser_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, form_parser_dup(fp));

  return ret;
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

  JSValue value = JS_UNDEFINED;
  int64_t index;

  if(js_atom_is_index(ctx, &index, prop)) {
    char* param_name;

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

    if((index = form_parser_param_index(fp, str)) != -1) {
      ret = TRUE;
      pdesc->flags = JS_PROP_ENUMERABLE;
      pdesc->value = JS_NewStringLen(ctx, lws_spa_get_string(fp->spa, index), lws_spa_get_length(fp->spa, index));
      pdesc->getter = JS_UNDEFINED;
      pdesc->setter = JS_UNDEFINED;
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
  int32_t entry;

  if(js_atom_is_index(ctx, &index, prop)) {
    char* param_name;

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

JSValue
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
    //.has_property = minnet_form_parser_has_property,
    .define_own_property = minnet_form_parser_define_own_property,
    //.get_property = minnet_form_parser_get_property,
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
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetFormParser", JS_PROP_CONFIGURABLE),
};

const size_t minnet_form_parser_proto_funcs_size = countof(minnet_form_parser_proto_funcs);

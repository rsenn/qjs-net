#define _GNU_SOURCE
#include <quickjs.h>
#include <cutils.h>
#include "minnet-form-parser.h"
#include "minnet-ringbuffer.h"
#include "jsutils.h"
#include <ctype.h>
#include <strings.h>
#include <libwebsockets.h>

THREAD_LOCAL JSClassID minnet_form_parser_class_id;
THREAD_LOCAL JSValue minnet_form_parser_proto, minnet_form_parser_ctor;

enum { FORM_PARSER_TYPE, FORM_PARSER_METHOD, FORM_PARSER_URI, FORM_PARSER_PATH, FORM_PARSER_HEADERS, FORM_PARSER_ARRAYBUFFER, FORM_PARSER_TEXT, FORM_PARSER_BODY };

void
form_parser_init(MinnetFormParser* fp, struct lws* wsi, int nparams, const char* const* param_names) {
  memset(&fp->spa_create_info, 0, sizeof(struct lws_spa_create_info));
  fp->spa_create_info.count_params = nparams;
  fp->spa_create_info.param_names = param_names;
  fp->spa_create_info.ac = &fp->lwsac_head;
  fp->spa_create_info.ac_chunk_size = 512;

  fp->spa = lws_spa_create_via_info(wsi, &fp->spa_create_info);
}

MinnetFormParser*
form_parser_alloc(JSContext* ctx) {
  MinnetFormParser* ret;

  ret = js_mallocz(ctx, sizeof(MinnetFormParser));
  ret->ref_count = 1;
  return ret;
}

MinnetFormParser*
form_parser_new(JSContext* ctx, struct lws* wsi, int nparams, const char* const* param_names) {
  MinnetFormParser* fp;

  if((fp = form_parser_alloc(ctx)))
    form_parser_init(fp, wsi, nparams, param_names);

  return fp;
}

MinnetFormParser*
form_parser_dup(MinnetFormParser* fp) {
  ++fp->ref_count;
  return fp;
}

void
form_parser_zero(MinnetFormParser* fp) {
  fp->spa = 0;
  memset(&fp->spa_create_info, 0, sizeof(struct lws_spa_create_info));
}

void
form_parser_clear(MinnetFormParser* fp, JSContext* ctx) {

  if(fp->spa) {
    lws_spa_destroy(fp->spa);
    fp->spa = 0;
  }

  if(fp->spa_create_info.param_names) {
    js_free(ctx, fp->spa_create_info.param_names);
  }
  memset(&fp->spa_create_info, 0, sizeof(struct lws_spa_create_info));
}

void
form_parser_clear_rt(MinnetFormParser* fp, JSRuntime* rt) {

  if(fp->spa) {
    lws_spa_destroy(fp->spa);
    fp->spa = 0;
  }

  if(fp->spa_create_info.param_names) {
    js_free_rt(rt, fp->spa_create_info.param_names);
  }
  memset(&fp->spa_create_info, 0, sizeof(struct lws_spa_create_info));
}

void
form_parser_free(MinnetFormParser* fp, JSContext* ctx) {
  if(--fp->ref_count == 0) {
    form_parser_clear(fp, ctx);
    js_free(ctx, fp);
  }
}

void
form_parser_free_rt(MinnetFormParser* fp, JSRuntime* rt) {
  if(--fp->ref_count == 0) {
    form_parser_clear_rt(fp, rt);
    js_free_rt(rt, fp);
  }
}

JSValue
minnet_form_parser_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj;
  MinnetFormParser* fp;
  BOOL got_url = FALSE;

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

  JS_SetOpaque(obj, fp);

  return obj;

fail:
  js_free(ctx, fp);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
minnet_form_parser_new(JSContext* ctx, struct lws* wsi, int nparams, const char* const* param_names) {
  MinnetFormParser* fp;

  if(!(fp = form_parser_new(ctx, wsi, nparams, param_names)))
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

  switch(magic) {}
  return ret;
}

static JSValue
minnet_form_parser_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetFormParser* fp;
  JSValue ret = JS_UNDEFINED;
  const char* str;
  size_t len;
  if(!(fp = minnet_form_parser_data2(ctx, this_val)))
    return JS_EXCEPTION;

  str = JS_ToCStringLen(ctx, &len, value);

  switch(magic) {}

  JS_FreeCString(ctx, str);

  return ret;
}

static void
minnet_form_parser_finalizer(JSRuntime* rt, JSValue val) {
  MinnetFormParser* fp;

  if((fp = minnet_form_parser_data(val)))
    form_parser_free_rt(fp, rt);
}

JSClassDef minnet_form_parser_class = {
    "MinnetFormParser",
    .finalizer = minnet_form_parser_finalizer,
};

const JSCFunctionListEntry minnet_form_parser_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetFormParser", JS_PROP_CONFIGURABLE),
};

const size_t minnet_form_parser_proto_funcs_size = countof(minnet_form_parser_proto_funcs);

/**
 * @file formparser.c
 */
#include "formparser.h"
#include "js-utils.h"
#include "utils.h"
#include "ws.h"

static int
formparser_callback(void* data, const char* name, const char* filename, char* buf, int len, enum lws_spa_fileupload_states state) {
  FormParser* fp = data;
  JSCallback* cb = 0;
  JSValue args[2] = {JS_NULL, JS_NULL};

  switch(state) {
    case LWS_UFS_CONTENT:
    case LWS_UFS_FINAL_CONTENT: {
      cb = &fp->cb.content;

      if(cb->ctx)
        if(len > 0)
          args[1] = JS_NewArrayBufferCopy(cb->ctx, (uint8_t*)buf, len);

      break;
    }

    case LWS_UFS_OPEN: {
      cb = &fp->cb.open;

      if(cb->ctx) {
        if(!JS_IsUndefined(fp->file)) {
          if(fp->cb.close.ctx) {
            args[1] = fp->file;
            JSValue ret = callback_emit(&fp->cb.close, 2, args);
            JS_FreeValue(cb->ctx, ret);
          }

          JS_FreeValue(cb->ctx, fp->file);
          fp->file = JS_UNDEFINED;
        }

        if(filename) {
          fp->file = JS_NewString(cb->ctx, filename);
          args[1] = JS_DupValue(cb->ctx, fp->file);
        }

        if(!JS_IsUndefined(fp->name)) {
          JS_FreeValue(cb->ctx, fp->name);
          fp->name = JS_UNDEFINED;
        }

        if(name)
          fp->name = JS_NewString(cb->ctx, name);
      }

      break;
    }

    case LWS_UFS_CLOSE: {
      cb = &fp->cb.close;

      if(cb->ctx) {
        // args[0] = JS_DupValue(cb->ctx, fp->name);

        if(!JS_IsUndefined(fp->file))
          args[1] = JS_DupValue(cb->ctx, fp->file);

        JS_FreeValue(cb->ctx, fp->file);
        fp->file = JS_UNDEFINED;
      }
      break;
    }
  }

  if(cb && cb->ctx) {
    JSValue ret;

    /*if(JS_IsUndefined(fp->name) && name)
    args[0] = JS_NewString(cb->ctx, name);
     else*/

    if(JS_IsUndefined(fp->name))
      args[0] = JS_DupValue(cb->ctx, fp->name);

    ret = callback_emit(cb, 2, args);

    if(JS_IsException(ret))
      js_error_print(cb->ctx, fp->exception = JS_GetException(cb->ctx));

    JS_FreeValue(cb->ctx, args[0]);
    JS_FreeValue(cb->ctx, args[1]);
  }

  return 0;
}

void
formparser_init(FormParser* fp, struct socket* ws, int nparams, const char* const* param_names, size_t chunk_size) {

  memset(&fp->spa_create_info, 0, sizeof(struct lws_spa_create_info));
  fp->ws = ws_dup(ws);
  fp->spa_create_info.count_params = nparams;
  fp->spa_create_info.param_names = param_names;
  fp->spa_create_info.max_storage = chunk_size + 1;
  fp->spa_create_info.opt_cb = &formparser_callback;
  fp->spa_create_info.opt_data = fp;

  fp->spa = lws_spa_create_via_info(ws->lwsi, &fp->spa_create_info);
  fp->exception = JS_NULL;
  fp->name = JS_UNDEFINED;
  fp->file = JS_UNDEFINED;
}

FormParser*
formparser_alloc(JSContext* ctx) {
  FormParser* ret;

  ret = js_mallocz(ctx, sizeof(FormParser));
  ret->ref_count = 1;
  return ret;
}

void
formparser_clear(FormParser* fp, JSRuntime* rt) {
  if(fp->spa) {
    lws_spa_destroy(fp->spa);
    fp->spa = 0;
  }

  if(fp->spa_create_info.param_names)
    js_free_rt(rt, (void*)fp->spa_create_info.param_names);

  memset(&fp->spa_create_info, 0, sizeof(struct lws_spa_create_info));

  FREECB_RT(fp->cb.content);
  FREECB_RT(fp->cb.open);
  FREECB_RT(fp->cb.close);
}

void
formparser_free(FormParser* fp, JSRuntime* rt) {
  if(--fp->ref_count == 0) {
    ws_free(fp->ws, rt);
    formparser_clear(fp, rt);
    js_free_rt(rt, fp);
  }
}

const char*
formparser_param_name(FormParser* fp, int index) {
  if(index >= 0 && index < fp->spa_create_info.count_params)
    return fp->spa_create_info.param_names[index];

  return 0;
}

bool
formparser_param_valid(FormParser* fp, int index) {
  if(index >= 0 && index < fp->spa_create_info.count_params)
    return true;

  return false;
}

size_t
formparser_param_count(FormParser* fp) {
  return fp->spa_create_info.count_params;
}

int
formparser_param_index(FormParser* fp, const char* name) {
  int i;

  for(i = 0; i < fp->spa_create_info.count_params; i++)
    if(!strcmp(fp->spa_create_info.param_names[i], name))
      return i;

  return -1;
}

bool
formparser_param_exists(FormParser* fp, const char* name) {
  int i = formparser_param_index(fp, name);

  return i != -1;
}

int
formparser_process(FormParser* fp, const void* data, size_t len) {
  int retval = lws_spa_process(fp->spa, data, len);
  fp->read += len;

  return retval;
}

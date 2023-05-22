/**
 * @file callback.c
 */
#include "callback.h"
#include "js-utils.h"
#include "utils.h"
#include "opaque.h"
#include <assert.h>

JSValue
callback_emit_this(const JSCallback* cb, JSValueConst this_obj, int argc, JSValue* argv) {
  JSValue ret = JS_UNDEFINED;

  if(cb->ctx)
    ret = JS_Call(cb->ctx, cb->func_obj, this_obj, argc, argv);

  /*  if(JS_IsException(ret)) {
      JSValue exception = JS_GetException(cb->ctx);
      js_error_print(cb->ctx, exception);
      ret = JS_Throw(cb->ctx, exception);
    }*/
  return ret;
}

JSValue
callback_emit(const JSCallback* cb, int argc, JSValue* argv) {
  return callback_emit_this(cb, cb->this_obj, argc, argv);
}

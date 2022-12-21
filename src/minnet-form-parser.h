#ifndef MINNET_FORM_PARSER_H
#define MINNET_FORM_PARSER_H

#include "form-parser.h"
#include "minnet-websocket.h"
#include "utils.h"

typedef struct form_parser MinnetFormParser;

JSValue minnet_form_parser_constructor(JSContext*, JSValueConst new_target, int argc, JSValueConst argv[]);
JSValue minnet_form_parser_new(JSContext*, MinnetWebsocket* ws, int nparams, const char* const* param_names, size_t chunk_size);
JSValue minnet_form_parser_wrap(JSContext*, MinnetFormParser* fp);
JSValue minnet_form_parser_call(JSContext*, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst argv[], int flags);

extern THREAD_LOCAL JSValue minnet_form_parser_proto, minnet_form_parser_ctor;
extern THREAD_LOCAL JSClassID minnet_form_parser_class_id;
extern JSClassDef minnet_form_parser_class;
extern const JSCFunctionListEntry minnet_form_parser_proto_funcs[];
extern const size_t minnet_form_parser_proto_funcs_size;

static inline MinnetFormParser*
minnet_form_parser_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_form_parser_class_id);
}

static inline MinnetFormParser*
minnet_form_parser_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_form_parser_class_id);
}
#endif /* MINNET_FORM_PARSER_H */

#ifndef MINNET_FORM_PARSER_H
#define MINNET_FORM_PARSER_H

#include "form-parser.h"
#include "minnet-websocket.h"
#include "utils.h"

typedef struct form_parser MinnetFormParser;

JSValue minnet_form_parser_constructor(JSContext*, JSValueConst, int, JSValueConst[]);
int minnet_form_parser_init(JSContext*, JSModuleDef*);

extern THREAD_LOCAL JSValue minnet_form_parser_proto, minnet_form_parser_ctor;
extern THREAD_LOCAL JSClassID minnet_form_parser_class_id;
extern JSClassDef minnet_form_parser_class;
extern const JSCFunctionListEntry minnet_form_parser_proto_funcs[];

static inline MinnetFormParser*
minnet_form_parser_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_form_parser_class_id);
}

static inline MinnetFormParser*
minnet_form_parser_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_form_parser_class_id);
}
#endif /* MINNET_FORM_PARSER_H */

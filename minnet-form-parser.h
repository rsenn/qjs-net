#ifndef MINNET_FORM_PARSER_H
#define MINNET_FORM_PARSER_H

#include <quickjs.h>
#include <cutils.h>
#include "jsutils.h"
#include "minnet-generator.h"

typedef struct form_parser {
  int ref_count;
  struct lws_spa_create_info spa_create_info;
  struct lws_spa* spa;
  struct lwsac* lwsac_head;

} MinnetFormParser;

void form_parser_init(MinnetFormParser*, struct lws*, int, const char* const* param_names);
MinnetFormParser* form_parser_alloc(JSContext*);
MinnetFormParser* form_parser_new(JSContext*, struct lws* wsi, int nparams, const char* const* param_names);
MinnetFormParser* form_parser_dup(MinnetFormParser*);
void form_parser_zero(MinnetFormParser*);
void form_parser_clear(MinnetFormParser*, JSContext*);
void form_parser_clear_rt(MinnetFormParser*, JSRuntime*);
void form_parser_free(MinnetFormParser*, JSContext*);
void form_parser_free_rt(MinnetFormParser*, JSRuntime*);
JSValueConst minnet_form_parser_constructor(JSContext*, JSValueConst, int, JSValueConst argv[]);
JSValueConst minnet_form_parser_new(JSContext*, struct lws* wsi, int nparams, const char* const* param_names);
JSValueConst minnet_form_parser_wrap(JSContext*, MinnetFormParser*);

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

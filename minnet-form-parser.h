#ifndef MINNET_FORM_PARSER_H
#define MINNET_FORM_PARSER_H

#include <quickjs.h>
#include <cutils.h>
#include "jsutils.h"
#include "minnet-generator.h"
#include "minnet-websocket.h"
#include "callback.h"

typedef struct form_parser {
  int ref_count;
  struct lws_spa_create_info spa_create_info;
  struct lws_spa* spa;
  struct lwsac* lwsac_head;
  MinnetWebsocket* ws;
  struct {
    JSCallback content, open, close, finalize;
  } cb;
  JSValue exception;
  JSValue name, file;
  size_t read;
} MinnetFormParser;

void form_parser_init(MinnetFormParser*, MinnetWebsocket*, int, const char* const* param_names, size_t chunk_size);
MinnetFormParser* form_parser_alloc(JSContext*);
MinnetFormParser* form_parser_new(JSContext*, MinnetWebsocket*, int, const char* const* param_names, size_t chunk_size);
MinnetFormParser* form_parser_dup(MinnetFormParser*);
void form_parser_zero(MinnetFormParser*);
void form_parser_clear(MinnetFormParser*, JSContext*);
void form_parser_clear_rt(MinnetFormParser*, JSRuntime*);
void form_parser_free(MinnetFormParser*, JSContext*);
void form_parser_free_rt(MinnetFormParser*, JSRuntime*);
const char* form_parser_param_name(MinnetFormParser*, int);
BOOL form_parser_param_valid(MinnetFormParser*, int);
size_t form_parser_param_count(MinnetFormParser*);
int form_parser_param_index(MinnetFormParser*, const char*);
BOOL form_parser_param_exists(MinnetFormParser*, const char*);
int form_parser_process(MinnetFormParser*, const void*, size_t);
JSValueConst minnet_form_parser_constructor(JSContext*, JSValueConst, int, JSValueConst argv[]);
JSValueConst minnet_form_parser_new(JSContext*, MinnetWebsocket*, int, const char* const* param_names, size_t chunk_size);
JSValueConst minnet_form_parser_wrap(JSContext*, MinnetFormParser*);
JSValueConst minnet_form_parser_call(JSContext*, JSValueConst, JSValueConst, int argc, JSValueConst argv[], int flags);

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

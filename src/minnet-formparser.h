#ifndef MINNET_FORMPARSER_H
#define MINNET_FORMPARSER_H

#include "formparser.h"
#include "minnet-websocket.h"
#include "utils.h"

typedef struct form_parser MinnetFormParser;

JSValue minnet_formparser_constructor(JSContext*, JSValueConst, int, JSValueConst[]);
int minnet_formparser_init(JSContext*, JSModuleDef*);

extern THREAD_LOCAL JSValue minnet_formparser_proto, minnet_formparser_ctor;
extern THREAD_LOCAL JSClassID minnet_formparser_class_id;

static inline MinnetFormParser*
minnet_formparser_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_formparser_class_id);
}

static inline MinnetFormParser*
minnet_formparser_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_formparser_class_id);
}
#endif /* MINNET_FORMPARSER_H */

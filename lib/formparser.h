#ifndef QJSNET_LIB_FORM_PARSER_H
#define QJSNET_LIB_FORM_PARSER_H

#include <libwebsockets.h>
#include <stdbool.h>
#include "callback.h"

typedef struct form_parser {
  int ref_count;
  struct lws_spa_create_info spa_create_info;
  struct lws_spa* spa;
  struct lwsac* lwsac_head;
  struct socket* ws;
  struct {
    JSCallback content, open, close, finalize;
  } cb;
  JSValue exception;
  JSValue name, file;
  size_t read;
} FormParser;

void formparser_init(FormParser*, struct socket* ws, int nparams, const char* const* param_names, size_t chunk_size);
FormParser* formparser_alloc(JSContext*);
void formparser_clear(FormParser*, JSContext* ctx);
void formparser_clear_rt(FormParser*, JSRuntime* rt);
void formparser_free(FormParser*, JSContext* ctx);
void formparser_free_rt(FormParser*, JSRuntime* rt);
const char* formparser_param_name(FormParser*, int index);
bool formparser_param_valid(FormParser*, int index);
size_t formparser_param_count(FormParser*);
int formparser_param_index(FormParser*, const char* name);
bool formparser_param_exists(FormParser*, const char* name);
int formparser_process(FormParser*, const void* data, size_t len);

#endif /* QJSNET_LIB_FORM_PARSER_H */

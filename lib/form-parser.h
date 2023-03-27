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

void        form_parser_init(FormParser*, struct socket* ws, int nparams, const char* const* param_names, size_t chunk_size);
FormParser* form_parser_alloc(JSContext*);
void        form_parser_clear(FormParser*, JSContext* ctx);
void        form_parser_clear_rt(FormParser*, JSRuntime* rt);
void        form_parser_free(FormParser*, JSContext* ctx);
void        form_parser_free_rt(FormParser*, JSRuntime* rt);
const char* form_parser_param_name(FormParser*, int index);
bool       form_parser_param_valid(FormParser*, int index);
size_t      form_parser_param_count(FormParser*);
int         form_parser_param_index(FormParser*, const char* name);
bool       form_parser_param_exists(FormParser*, const char* name);
int         form_parser_process(FormParser*, const void* data, size_t len);


#endif /* QJSNET_LIB_FORM_PARSER_H */

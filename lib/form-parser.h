#ifndef QJSNET_LIB_FORM_PARSER_H
#define QJSNET_LIB_FORM_PARSER_H

#include <libwebsockets.h>
#include <stdbool.h>
#include "callback.h"

struct form_parser {
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
};

void                form_parser_init(struct form_parser*, struct socket* ws, int nparams, const char* const* param_names, size_t chunk_size);
struct form_parser* form_parser_alloc(JSContext*);
struct form_parser* form_parser_dup(struct form_parser*);
void                form_parser_clear(struct form_parser*, JSContext* ctx);
void                form_parser_clear_rt(struct form_parser*, JSRuntime* rt);
void                form_parser_free(struct form_parser*, JSContext* ctx);
void                form_parser_free_rt(struct form_parser*, JSRuntime* rt);
const char*         form_parser_param_name(struct form_parser*, int index);
_Bool               form_parser_param_valid(struct form_parser*, int index);
size_t              form_parser_param_count(struct form_parser*);
int                 form_parser_param_index(struct form_parser*, const char* name);
_Bool               form_parser_param_exists(struct form_parser*, const char* name);
int                 form_parser_process(struct form_parser*, const void* data, size_t len);

#endif /* QJSNET_LIB_FORM_PARSER_H */

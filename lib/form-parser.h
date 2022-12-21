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

void form_parser_init(struct form_parser*, struct socket*, int, const char* const* param_names, size_t chunk_size);
struct form_parser* form_parser_alloc(JSContext*);
struct form_parser* form_parser_new(JSContext*, struct socket*, int, const char* const* param_names, size_t chunk_size);
struct form_parser* form_parser_dup(struct form_parser*);
void form_parser_zero(struct form_parser*);
void form_parser_clear(struct form_parser*, JSContext*);
void form_parser_clear_rt(struct form_parser*, JSRuntime*);
void form_parser_free(struct form_parser*, JSContext*);
void form_parser_free_rt(struct form_parser*, JSRuntime*);
const char* form_parser_param_name(struct form_parser*, int);
bool form_parser_param_valid(struct form_parser*, int);
size_t form_parser_param_count(struct form_parser*);
int form_parser_param_index(struct form_parser*, const char*);
bool form_parser_param_exists(struct form_parser*, const char*);
int form_parser_process(struct form_parser*, const void*, size_t);

#endif /* QJSNET_LIB_FORM_PARSER_H */

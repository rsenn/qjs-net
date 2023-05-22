/**
 * @file response.h
 */
#ifndef QJSNET_LIB_RESPONSE_H
#define QJSNET_LIB_RESPONSE_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include "url.h"
#include "buffer.h"
#include "generator.h"

struct session_data;

typedef struct http_response {
  int ref_count;
  bool read_only : 1, headers_sent : 1, compress : 1;
  URL url;
  int status;
  char* status_text;
  ByteBuffer headers;
  Generator* body;
} Response;

void response_zero(Response*);
void response_init(Response*, URL, int32_t, char* status_text, BOOL headers_sent, char* type);
Response* response_dup(Response*);
void response_clear(Response*, JSRuntime*);
void response_free(Response*, JSRuntime*);
Response* response_new(JSContext*);
ssize_t response_settype(Response*, const char*);
char* response_type(Response*, JSContext*);
void response_redirect(Response* resp, int code, const char* location);

static inline Generator*
response_generator(struct http_response* resp, JSContext* ctx) {
  if(!resp->body)
    resp->body = generator_new(ctx);
  return resp->body;
}

struct http_response* response_new(JSContext*);

#endif /* QJSNET_LIB_RESPONSE_H */

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
  struct url url;
  char* type;
  int status;
  char* status_text;
  ByteBuffer headers;
  Generator* generator;
} Response;

void response_init(struct http_response*, struct url, int32_t, char* status_text, BOOL headers_sent, char* type);
struct http_response* response_dup(struct http_response*);
void response_clear(struct http_response*, JSContext*);
void response_clear_rt(struct http_response*, JSRuntime*);
void response_free(struct http_response*, JSContext*);
void response_free_rt(struct http_response*, JSRuntime*);

static inline Generator*
response_generator(struct http_response* resp, JSContext* ctx) {
  if(!resp->generator)
    resp->generator = generator_new(ctx);
  return resp->generator;
}

struct http_response* response_new(JSContext*);

#endif /* QJSNET_LIB_RESPONSE_H */

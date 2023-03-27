#ifndef QJSNET_LIB_RESPONSE_H
#define QJSNET_LIB_RESPONSE_H

#include <cutils.h>
#include <quickjs.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "buffer.h"
#include "callback.h"
#include "generator.h"
#include "url.h"

struct session_data;

typedef struct http_response {
  int ref_count;
  BOOL read_only, headers_sent, compress;
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

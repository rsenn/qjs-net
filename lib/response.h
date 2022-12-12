#ifndef QJSNET_LIB_RESPONSE_H
#define QJSNET_LIB_RESPONSE_H

#include <quickjs.h>
#include <list.h>
#include <libwebsockets.h>
#include "url.h"
#include "buffer.h"
#include "generator.h"
#include "session.h"

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

void response_format(Response const*, char*, size_t);
char* response_dump(Response const*);
void response_init(Response*, struct url, int32_t, char* status_text, BOOL headers_sent, char* type);
Response* response_dup(Response*);
Response* response_redirect(Response* resp, const char* location, JSContext* ctx);
ssize_t response_write(Response*, const void*, size_t, JSContext* ctx);
void response_clear(Response*, JSContext*);
void response_clear_rt(Response*, JSRuntime*);
void response_free(Response*, JSContext*);
void response_free_rt(Response*, JSRuntime*);

static inline Generator*
response_generator(Response* resp, JSContext* ctx) {
  if(!resp->generator)
    resp->generator = generator_new(ctx);
  return resp->generator;
}

Response* response_new(JSContext*);
Response* session_response(struct session_data*, JSCallback*);

#endif /* QJSNET_LIB_RESPONSE_H */

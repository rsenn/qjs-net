#ifndef QJSNET_LIB_RESPONSE_H
#define QJSNET_LIB_RESPONSE_H

#include <cutils.h>    // for BOOL
#include <quickjs.h>   // for JSContext, JSRuntime
#include <stddef.h>    // for size_t
#include <stdint.h>    // for int32_t
#include <sys/types.h> // for ssize_t
#include "buffer.h"    // for ByteBuffer
#include "callback.h"  // for JSCallback
#include "generator.h" // for generator_new, Generator
#include "url.h"       // for url

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

void response_format(const struct http_response*, char*, size_t);
char* response_dump(const struct http_response*);
void response_init(struct http_response*, struct url, int32_t, char* status_text, BOOL headers_sent, char* type);
struct http_response* response_dup(struct http_response*);
struct http_response* response_redirect(struct http_response* resp, const char* location, JSContext* ctx);
ssize_t response_write(struct http_response*, const void*, size_t, JSContext* ctx);
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

struct http_response* response_session(struct http_response* resp, struct session_data* session, JSCallback* cb);

#endif /* QJSNET_LIB_RESPONSE_H */

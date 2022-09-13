#ifndef WJSNET_LIBQJSNET_LIB_RESPONSE_H
#define WJSNET_LIBQJSNET_LIB_RESPONSE_H

#include <quickjs.h>
#include <list.h>
#include <libwebsockets.h>
#include "url.h"
#include "buffer.h"
#include "../minnet-generator.h"
#include "session.h"

struct http_response {
  int ref_count;
  BOOL read_only;
  struct url url;
  char* type;
  int status;
  char* status_text;
  BOOL ok;
  ByteBuffer headers;
  union {
    struct generator* generator;
    ByteBuffer* body;
  };
};

void response_format(struct http_response const*, char*, size_t);
char* response_dump(struct http_response const*);
void response_init(struct http_response*, struct url, int32_t, char* status_text, BOOL ok, char* type);
struct http_response* response_dup(struct http_response*);
struct http_response* response_redirect(struct http_response* resp, const char* location, JSContext* ctx);
ssize_t response_write(struct http_response*, const void*, size_t, JSContext* ctx);
void response_clear(struct http_response*, JSContext*);
void response_clear_rt(struct http_response*, JSRuntime*);
void response_free(struct http_response*, JSContext*);
void response_free_rt(struct http_response*, JSRuntime*);
static inline struct generator*
response_generator(struct http_response* resp, JSContext* ctx) {
  if(!resp->generator)
    resp->generator = generator_new(ctx);
  return resp->generator;
}

struct http_response* response_new(JSContext*);
struct http_response* session_response(struct session_data*, JSCallback*);

#endif /* WJSNET_LIBQJSNET_LIB_RESPONSE_H */

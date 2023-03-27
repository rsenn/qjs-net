#include "session.h"
#include "response.h"
#include "buffer.h"
#include "jsutils.h"
#include "headers.h"
#include <assert.h>

struct http_response* minnet_response_data(JSValueConst);

void
response_init(struct http_response* resp, struct url url, int32_t status, char* status_text, BOOL headers_sent, char* type) {
  // memset(resp, 0, sizeof(Response));

  resp->status = status;
  resp->status_text = status_text;
  resp->headers_sent = headers_sent;
  resp->url = url;
  resp->type = type;
  resp->headers = BUFFER_0();
  resp->generator = NULL;
}

struct http_response*
response_dup(struct http_response* resp) {
  ++resp->ref_count;
  return resp;
}

void
response_clear(struct http_response* resp, JSContext* ctx) {
  url_free(&resp->url, ctx);
  if(resp->type) {
    js_free(ctx, (void*)resp->type);
    resp->type = 0;
  }

  buffer_free(&resp->headers);
  generator_destroy(&resp->generator);
}

void
response_clear_rt(struct http_response* resp, JSRuntime* rt) {
  url_free_rt(&resp->url, rt);
  if(resp->type) {
    js_free_rt(rt, (void*)resp->type);
    resp->type = 0;
  }

  buffer_free(&resp->headers);
  generator_destroy(&resp->generator);
}

void
response_free(struct http_response* resp, JSContext* ctx) {
  if(--resp->ref_count == 0) {
    response_clear(resp, ctx);
    js_free(ctx, resp);
  }
}

void
response_free_rt(struct http_response* resp, JSRuntime* rt) {
  if(--resp->ref_count == 0) {
    response_clear_rt(resp, rt);
    js_free_rt(rt, resp);
  }
}

struct http_response*
response_new(JSContext* ctx) {
  struct http_response* resp;

  if(!(resp = js_mallocz(ctx, sizeof(Response))))
    JS_ThrowOutOfMemory(ctx);

  resp->ref_count = 1;

  return resp;
}

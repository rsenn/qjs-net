#include "session.h"
#include "response.h"
#include "buffer.h"
#include "js-utils.h"
#include "headers.h"
#include <assert.h>

void
response_init(Response* resp, URL url, int32_t status, char* status_text, BOOL headers_sent, char* type) {
  // memset(resp, 0, sizeof(Response));

  resp->status = status;
  resp->status_text = status_text;
  resp->headers_sent = headers_sent;
  resp->url = url;
  resp->headers = BUFFER_0();
  resp->body = NULL;
}

Response*
response_dup(Response* resp) {
  ++resp->ref_count;
  return resp;
}

void
response_clear(Response* resp, JSRuntime* rt) {
  url_free_rt(&resp->url, rt);
  buffer_free(&resp->headers);
  if(resp->body) {
    generator_free(resp->body);
    resp->body = 0;
  }
}

void
response_free(Response* resp, JSRuntime* rt) {
  if(--resp->ref_count == 0) {
    response_clear(resp, rt);
    js_free_rt(rt, resp);
  }
}

Response*
response_new(JSContext* ctx) {
  Response* resp;

  if((resp = js_mallocz(ctx, sizeof(Response))))
    resp->ref_count = 1;

  return resp;
}

ssize_t
response_settype(Response* resp, const char* type) {
  return headers_set(&resp->headers, "content-type", type, "\r\n");
}

void
response_redirect(Response* resp, int code, const char* location) {
  resp->status = code;
  headers_set(&resp->headers, "location", location, "\r\n");
}

char*
response_type(Response* resp, JSContext* ctx) {
  return headers_get(&resp->headers, "content-type", "\r\n", ":", ctx);
}

#include "ws.h"
#include "../minnet-response.h"
#include "buffer.h"
#include "jsutils.h"
#include "headers.h"
#include <cutils.h>
#include <assert.h>

void
response_format(struct http_response const* resp, char* buf, size_t len) {
  snprintf(buf, len, FGC(226, "struct http_response") " { url.path: '%s', status: %d, ok: %s, type: '%s' }", resp->url.path, resp->status, resp->ok ? "true" : "false", resp->type);
}

char*
response_dump(struct http_response const* resp) {
  static char buf[1024];
  response_format(resp, buf, sizeof(buf));
  return buf;
}

/*void
response_zero(struct http_response* resp) {
  memset(resp, 0, sizeof(struct http_response));
  resp->body = BUFFER_0();
}*/

void
response_init(struct http_response* resp, struct url url, int32_t status, char* status_text, BOOL ok, char* type) {
  // memset(resp, 0, sizeof(struct http_response));

  resp->status = status;
  resp->status_text = status_text;
  resp->ok = ok;
  resp->url = url;
  resp->type = type;
  resp->headers = BUFFER_0();
  resp->body = 0; // BUFFER_0();
}

struct http_response*
response_dup(struct http_response* resp) {
  ++resp->ref_count;
  return resp;
}

ssize_t
response_write(struct http_response* resp, const void* x, size_t n, JSContext* ctx) {
  assert(resp->body);
  return buffer_append(resp->body, x, n, ctx);
}

void
response_clear(struct http_response* resp, JSContext* ctx) {
  url_free(&resp->url, ctx);
  if(resp->type) {
    js_free(ctx, (void*)resp->type);
    resp->type = 0;
  }

  buffer_free_rt(&resp->headers, JS_GetRuntime(ctx));
  generator_destroy(&resp->generator);
}

void
response_clear_rt(struct http_response* resp, JSRuntime* rt) {
  url_free_rt(&resp->url, rt);
  if(resp->type) {
    js_free_rt(rt, (void*)resp->type);
    resp->type = 0;
  }

  buffer_free_rt(&resp->headers, rt);
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

  if(!(resp = js_mallocz(ctx, sizeof(struct http_response))))
    JS_ThrowOutOfMemory(ctx);

  resp->ref_count = 1;

  return resp;
}

struct http_response*
response_redirect(struct http_response* resp, const char* location, JSContext* ctx) {

  resp->status = 302;
  // url_parse(&resp->url, location, ctx);
  headers_set(ctx, &resp->headers, "Location", location);
  return resp;
}

struct http_response*
session_response(struct session_data* session, JSCallback* cb) {
  struct http_response* resp = minnet_response_data2(cb->ctx, session->resp_obj);

  if(cb && cb->ctx) {
    JSValue ret = callback_emit_this(cb, session->ws_obj, 2, session->args);
    lwsl_user("session_response ret=%s", JS_ToCString(cb->ctx, ret));
    if(JS_IsObject(ret) && minnet_response_data2(cb->ctx, ret)) {
      JS_FreeValue(cb->ctx, session->args[1]);
      session->args[1] = ret;
      resp = minnet_response_data2(cb->ctx, ret);
    } else {
      JS_FreeValue(cb->ctx, ret);
    }
  }
  lwsl_user("session_response %s", response_dump(resp));

  return resp;
}

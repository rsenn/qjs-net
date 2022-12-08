#include "ws.h"
#include "../minnet-response.h"
#include "buffer.h"
#include "jsutils.h"
#include "headers.h"
#include <cutils.h>
#include <assert.h>

void
response_format(Response const* resp, char* buf, size_t len) {
  snprintf(buf, len, FGC(226, "Response") " { url.path: '%s', status: %d, headers_sent: %s, type: '%s' }", resp->url.path, resp->status, resp->headers_sent ? "true" : "false", resp->type);
}

char*
response_dump(Response const* resp) {
  static char buf[1024];
  response_format(resp, buf, sizeof(buf));
  return buf;
}

/*void
response_zero(Response* resp) {
  memset(resp, 0, sizeof(Response));
  resp->body = BUFFER_0();
}*/

void
response_init(Response* resp, struct url url, int32_t status, char* status_text, BOOL headers_sent, char* type) {
  // memset(resp, 0, sizeof(Response));

  resp->status = status;
  resp->status_text = status_text;
  resp->headers_sent = headers_sent;
  resp->url = url;
  resp->type = type;
  resp->headers = BUFFER_0();
  resp->generator = NULL;
}

Response*
response_dup(Response* resp) {
  ++resp->ref_count;
  return resp;
}

ssize_t
response_write(Response* resp, const void* x, size_t n, JSContext* ctx) {
  assert(resp->generator);
  return generator_write(resp->generator, x, n);
}

void
response_clear(Response* resp, JSContext* ctx) {
  url_free(&resp->url, ctx);
  if(resp->type) {
    js_free(ctx, (void*)resp->type);
    resp->type = 0;
  }

  buffer_free_rt(&resp->headers, JS_GetRuntime(ctx));
  generator_destroy(&resp->generator);
}

void
response_clear_rt(Response* resp, JSRuntime* rt) {
  url_free_rt(&resp->url, rt);
  if(resp->type) {
    js_free_rt(rt, (void*)resp->type);
    resp->type = 0;
  }

  buffer_free_rt(&resp->headers, rt);
  generator_destroy(&resp->generator);
}

void
response_free(Response* resp, JSContext* ctx) {
  if(--resp->ref_count == 0) {
    response_clear(resp, ctx);
    js_free(ctx, resp);
  }
}

void
response_free_rt(Response* resp, JSRuntime* rt) {
  if(--resp->ref_count == 0) {
    response_clear_rt(resp, rt);
    js_free_rt(rt, resp);
  }
}

Response*
response_new(JSContext* ctx) {
  Response* resp;

  if(!(resp = js_mallocz(ctx, sizeof(Response))))
    JS_ThrowOutOfMemory(ctx);

  resp->ref_count = 1;

  return resp;
}

Response*
response_redirect(Response* resp, const char* location, JSContext* ctx) {

  resp->status = 302;
  // url_parse(&resp->url, location, ctx);
  headers_set(ctx, &resp->headers, "Location", location);
  return resp;
}

Response*
session_response(struct session_data* session, JSCallback* cb) {
  Response* resp = minnet_response_data2(cb->ctx, session->resp_obj);

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

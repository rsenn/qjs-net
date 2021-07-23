#ifndef MINNET_SERVER_H
#define MINNET_SERVER_H

#include "cutils.h"

typedef struct lws_http_mount MinnetHttpMount;

typedef struct http_body {
  char path[128];
  size_t times, budget, content_lines;
} MinnetHttpBody;

typedef struct http_request {
  char* uri;
  MinnetHttpBody body;
} MinnetHttpRequest;

typedef struct http_header {
  unsigned char *start, *pos, *end;
} MinnetHttpHeader;

JSValue minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

static void
http_header_alloc(JSContext* ctx, MinnetHttpHeader* hdr, size_t size) {
  hdr->start = js_malloc(ctx, size);
  hdr->pos = hdr->start;
  hdr->end = hdr->start + size;
}

static void
http_header_free(JSContext* ctx, MinnetHttpHeader* hdr) {
  js_free(ctx, hdr->start);
  hdr->start = 0;
  hdr->pos = 0;
  hdr->end = 0;
}

static int callback_ws(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
static int callback_http(struct lws*, enum lws_callback_reasons, void*, void*, size_t);

#endif /* MINNET_SERVER_H */

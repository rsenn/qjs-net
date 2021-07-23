#ifndef MINNET_SERVER_H
#define MINNET_SERVER_H

#include "cutils.h"

struct http_header {
  unsigned char *start, *pos, *end;
};

JSValue minnet_ws_server(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

static inline void
http_header_alloc(JSContext* ctx, struct http_header* hdr, size_t size) {
  hdr->start = js_malloc(ctx, size);
  hdr->pos = hdr->start;
  hdr->end = hdr->start + size;
}

static inline void
http_header_free(JSContext* ctx, struct http_header* hdr) {
  js_free(ctx, hdr->start);
  hdr->start = 0;
  hdr->pos = 0;
  hdr->end = 0;
}

#endif /* MINNET_SERVER_H */
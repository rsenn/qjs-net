#include "buffer.h"
#include <libwebsockets.h>

void
buffer_init(struct byte_buffer* buf, uint8_t* start, size_t len) {
  buf->start = start;
  buf->rdpos = buf->start;
  buf->wrpos = buf->start;
  buf->end = start + len;
}

struct byte_buffer*
buffer_new(JSContext* ctx, size_t size) {
  if(size < LWS_RECOMMENDED_MIN_HEADER_SPACE)
    size = LWS_RECOMMENDED_MIN_HEADER_SPACE;
  struct byte_buffer* buf = js_mallocz(ctx, sizeof(struct byte_buffer) + size + LWS_PRE);

  buffer_init(buf, (uint8_t*)&buf[1] + LWS_PRE, size);
  return buf;
}

BOOL
buffer_alloc(struct byte_buffer* buf, size_t size, JSContext* ctx) {
  uint8_t* p;
  if((p = js_malloc(ctx, size + LWS_PRE))) {
    buffer_init(buf, p + LWS_PRE, size);
    return TRUE;
  }
  return FALSE;
}

ssize_t
buffer_append(struct byte_buffer* buf, const void* x, size_t n, JSContext* ctx) {
  ssize_t ret = -1;
  if((size_t)buffer_AVAIL(buf) < n) {
    if(!buffer_realloc(buf, buffer_OFFSET(buf) + n + 1, ctx))
      return ret;
  }
  memcpy(buf->wrpos, x, n);
  buf->wrpos[n] = '\0';
  buf->wrpos += n;
  return n;
}

void
buffer_free(struct byte_buffer* buf, JSRuntime* rt) {
  uint8_t* start = buf->start;
  if(start == (uint8_t*)&buf[1]) {
    js_free_rt(rt, buf);
  } else {
    js_free_rt(rt, buf->start);
    buf->start = 0;
    buf->rdpos = 0;
    buf->wrpos = 0;
    buf->end = 0;
    js_free_rt(rt, buf);
  }
}

BOOL
buffer_write(struct byte_buffer* buf, const char* x, size_t n) {
  assert((size_t)buffer_AVAIL(buf) >= n);
  memcpy(buf->wrpos, x, n);
  buf->wrpos[n] = '\0';
  buf->wrpos += n;
  return TRUE;
}

int
buffer_printf(struct byte_buffer* buf, const char* format, ...) {
  va_list ap;
  int n;
  size_t size = lws_ptr_diff_size_t(buf->end, buf->wrpos);

  /* if(!(size = lws_ptr_diff_size_t(buf->end, buf->wrpos)))
     return 0;*/

  va_start(ap, format);
  n = vsnprintf((char*)buf->wrpos, size, format, ap);
  va_end(ap);

  if(n > size)
    return 0;

  if(n >= (int)size)
    n = size;

  buf->wrpos += n;

  return n;
}

uint8_t*
buffer_realloc(struct byte_buffer* buf, size_t size, JSContext* ctx) {
  assert((uint8_t*)&buf[1] != buf->start);
  size_t wrofs = buf->wrpos - buf->start;
  size_t rdofs = buf->rdpos - buf->start;
  assert(size >= wrofs);

  uint8_t* x = js_realloc(ctx, buf->start - LWS_PRE, size + LWS_PRE);

  if(x) {
    buf->start = x + LWS_PRE;
    buf->wrpos = buf->start + wrofs;
    buf->rdpos = buf->start + rdofs;
    buf->end = buf->start + size;
    return buf->start;
  }
  return 0;
}

int
buffer_fromarraybuffer(struct byte_buffer* buf, JSValueConst value, JSContext* ctx) {
  void* ptr;
  size_t len;
  if((ptr = JS_GetArrayBuffer(ctx, &len, value))) {
    buf->start = ptr;
    buf->rdpos = ptr;
    buf->wrpos = ptr;
    buf->end = buf->start + len;
    return 0;
  }
  return 1;
}

int
buffer_fromvalue(struct byte_buffer* buf, JSValueConst value, JSContext* ctx) {
  if(buffer_fromarraybuffer(buf, value, ctx)) {
    size_t len;
    const char* str = JS_ToCStringLen(ctx, &len, value);

    buf->wrpos = buf->start = (uint8_t*)str;
    buf->end = buf->start + len;
  }
  return 0;
}

JSValue
buffer_tostring(struct byte_buffer const* buf, JSContext* ctx) {
  void* ptr = buf->start;
  size_t len = buf->wrpos - buf->start;
  return JS_NewStringLen(ctx, ptr, len);
}

void
buffer_finalizer(JSRuntime* rt, void* opaque, void* ptr) {
  struct byte_buffer* buf = opaque;
}

JSValue
buffer_toarraybuffer(struct byte_buffer const* buf, JSContext* ctx) {
  void* ptr = buf->start;
  size_t len = buf->end - buf->start;
  return JS_NewArrayBuffer(ctx, ptr, len, buffer_finalizer, (void*)buf, FALSE);
}

void
buffer_dump(const char* n, struct byte_buffer const* buf) {
  fprintf(stderr, "%s\t{ wrpos = %zu, size = %zu }\n", n, buf->wrpos - buf->start, buf->end - buf->start);
  fflush(stderr);
}

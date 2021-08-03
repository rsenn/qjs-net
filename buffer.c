#include "buffer.h"
#include <libwebsockets.h>

void
buffer_init(struct byte_buffer* buf, uint8_t* start, size_t len) {
  buf->start = start;
  buf->pos = buf->start;
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

BOOL
buffer_append(struct byte_buffer* buf, const char* x, size_t n) {
  assert((size_t)buffer_AVAIL(buf) >= n);
  memcpy(buf->pos, x, n);
  buf->pos[n] = '\0';
  buf->pos += n;
  return TRUE;
}

int
buffer_printf(struct byte_buffer* buf, const char* format, ...) {
  va_list ap;
  int n;
  size_t size = lws_ptr_diff_size_t(buf->end, buf->pos);

  /* if(!(size = lws_ptr_diff_size_t(buf->end, buf->pos)))
     return 0;*/

  va_start(ap, format);
  n = vsnprintf((char*)buf->pos, size, format, ap);
  va_end(ap);

  if(n > size)
    return 0;

  if(n >= (int)size)
    n = size;

  buf->pos += n;

  return n;
}

uint8_t*
buffer_realloc(struct byte_buffer* buf, size_t size, JSContext* ctx) {
  assert((uint8_t*)&buf[1] != buf->start);
  size_t offset = buf->pos - buf->start;
  assert(size >= offset);

  uint8_t* x = js_realloc(ctx, buf->start - LWS_PRE, size + LWS_PRE);

  if(x) {
    buf->start = x + LWS_PRE;
    buf->pos = buf->start + offset;
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
    buf->pos = ptr;
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

    buf->pos = buf->start = (uint8_t*)str;
    buf->end = buf->start + len;
  }
  return 0;
}

void
buffer_dump(const char* n, struct byte_buffer const* buf) {
  fprintf(stderr, "%s\t{ pos = %zu, size = %zu }\n", n, buf->pos - buf->start, buf->end - buf->start);
  fflush(stderr);
}

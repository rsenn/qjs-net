#ifndef BUFFER_H
#define BUFFER_H

#include <assert.h>
#include <inttypes.h>
#include <string.h>

typedef struct byte_buffer {
  uint8_t *start, *pos, *end;
} MinnetBuffer;

#define BUFFER(buf)                                                                                                                                                                                    \
  (MinnetBuffer) { ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + sizeof((buf)) - 1 }

#define buffer_AVAIL(b)  ((b)->end - (b)->pos)
#define buffer_OFFSET(b)   ((b)->pos - (b)->start)
#define buffer_SIZE(b)  ((b)->end - (b)->start)
#define buffer_PTR(b) (void*)(b)->start

static inline void
buffer_init(struct byte_buffer* buf, uint8_t* start, size_t len) {
  buf->start = start + LWS_PRE;
  buf->pos = buf->start;
  buf->end = start + len;
}

static inline struct byte_buffer*
buffer_new(JSContext* ctx, size_t size) {
  if(size < LWS_RECOMMENDED_MIN_HEADER_SPACE)
    size = LWS_RECOMMENDED_MIN_HEADER_SPACE;
  size += LWS_PRE;
  struct byte_buffer* buf = js_mallocz(ctx, sizeof(struct byte_buffer) + size);

  buffer_init(buf, (uint8_t*)&buf[1], size);
  return buf;
}

static inline BOOL
buffer_alloc(struct byte_buffer* buf, size_t size, JSContext* ctx) {
  uint8_t* p;
  size += LWS_PRE;
  if((p = js_malloc(ctx, size))) {
    buffer_init(buf, p, size);
    return TRUE;
  }
  return FALSE;
}

static inline BOOL
buffer_append(struct byte_buffer* buf, const char* x, size_t n) {
  assert((size_t)buffer_AVAIL(buf) >= n);
  memcpy(buf->pos, x, n);
  buf->pos[n] = '\0';
  buf->pos += n;
  return TRUE;
}

static inline int
buffer_printf(struct byte_buffer* buf, const char* format, ...) {
  va_list ap;
  int n;
  size_t size = lws_ptr_diff_size_t(buf->end, buf->pos);

  if(!size)
    return 0;

  va_start(ap, format);
  n = vsnprintf((char*)buf->pos, size, format, ap);
  va_end(ap);

  if(n >= (int)size)
    n = size;

  buf->pos += n;

  return n;
}

static inline uint8_t*
buffer_realloc(JSContext* ctx, struct byte_buffer* buf, size_t size) {
  assert((uint8_t*)&buf[1] != buf->start);
  buf->start = js_realloc(ctx, buf->start, size);
  buf->end = buf->start + size;
  return buf->start;
}

static inline void
buffer_free(struct byte_buffer* buf, JSRuntime* rt) {
  uint8_t* start = buf->start;
  if(start == (uint8_t*)&buf[1]) {
    js_free_rt(rt, buf);
  } else {
    js_free_rt(rt, buf->start);
    buf->start = 0;
    buf->pos = 0;
    buf->end = 0;
    js_free_rt(rt, buf);
  }
}

static inline JSValue
buffer_tostring(struct byte_buffer const* buf, JSContext* ctx) {
  void* ptr = buf->start;
  size_t len = buf->pos - buf->start;
  return JS_NewStringLen(ctx, ptr, len);
}

static inline void
buffer_finalizer(JSRuntime* rt, void* opaque, void* ptr) {
  struct byte_buffer* buf = opaque;
}

static inline JSValue
buffer_tobuffer(struct byte_buffer const* buf, JSContext* ctx) {
  void* ptr = buf->start;
  size_t len = buf->end - buf->start;
  return JS_NewArrayBuffer(ctx, ptr, len, buffer_finalizer, (void*)buf, FALSE);
}

static inline void
buffer_dump(const char* n, struct byte_buffer const* buf) {
  fprintf(stderr, "%s\t{ pos = %zu, size = %zu }\n", n, buf->pos - buf->start, buf->end - buf->start);
  fflush(stderr);
}

#endif /* BUFFER_H */
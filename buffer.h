#ifndef BUFFER_H
#define BUFFER_H

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <string.h>
#include <quickjs.h>
#include <cutils.h>

typedef struct byte_buffer {
  uint8_t *start, *pos, *end;
} MinnetBuffer;

#define BUFFER(buf)                                                                                                                                                                                    \
  (MinnetBuffer) { ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + sizeof((buf)) - 1 }

#define BUFFER_0()                                                                                                                                                                                     \
  (MinnetBuffer) { 0, 0, 0 }

#define BUFFER_N(buf, n)                                                                                                                                                                               \
  (MinnetBuffer) { ((uint8_t*)(buf)), ((uint8_t*)(buf)) + n, ((uint8_t*)(buf)) + n }

#define buffer_AVAIL(b) ((b)->end - (b)->pos)
#define buffer_OFFSET(b) ((b)->pos - (b)->start)
#define buffer_SIZE(b) ((b)->end - (b)->start)
#define buffer_START(b) (void*)(b)->start

void buffer_init(struct byte_buffer*, uint8_t* start, size_t len);
struct byte_buffer* buffer_new(JSContext*, size_t size);
BOOL buffer_alloc(struct byte_buffer*, size_t size, JSContext* ctx);
BOOL buffer_append(struct byte_buffer*, const char* x, size_t n);
int buffer_printf(struct byte_buffer*, const char* format, ...);
uint8_t* buffer_realloc(struct byte_buffer*, size_t size, JSContext* ctx);
int buffer_fromarraybuffer(struct byte_buffer*, JSValue value, JSContext* ctx);
int buffer_fromvalue(struct byte_buffer*, JSValue value, JSContext* ctx);
void buffer_dump(const char*, struct byte_buffer const* buf);
int buffer_fromarraybuffer(struct byte_buffer*, JSValue value, JSContext* ctx);
int buffer_fromvalue(struct byte_buffer*, JSValue value, JSContext* ctx);
void buffer_dump(const char*, struct byte_buffer const* buf);

static inline uint8_t*
buffer_grow(struct byte_buffer* buf, size_t size, JSContext* ctx) {
  return buffer_realloc(buf, (buf->end - buf->start) + size, ctx);
}

static inline ssize_t
buffer_write(struct byte_buffer* buf, const void* x, size_t n, JSContext* ctx) {
  ssize_t ret = -1;
  if((size_t)buffer_AVAIL(buf) < n) {
    if(!buffer_realloc(buf, buffer_OFFSET(buf) + n + 1, ctx))
      return ret;
  }
  memcpy(buf->pos, x, n);
  buf->pos[n] = '\0';
  buf->pos += n;
  return n;
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
buffer_toarraybuffer(struct byte_buffer const* buf, JSContext* ctx) {
  void* ptr = buf->start;
  size_t len = buf->end - buf->start;
  return JS_NewArrayBuffer(ctx, ptr, len, buffer_finalizer, (void*)buf, FALSE);
}

#endif /* BUFFER_H */
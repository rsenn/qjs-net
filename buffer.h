#ifndef BUFFER_H
#define BUFFER_H

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <string.h>
#include <quickjs.h>
#include <cutils.h>

typedef struct byte_buffer {
  uint8_t *start, *wrpos, *rdpos, *end;
} MinnetBuffer;

#define BUFFER(buf)                                                                                                                                                                                    \
  (MinnetBuffer) { ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + sizeof((buf)) - 1 }

#define BUFFER_0()                                                                                                                                                                                     \
  (MinnetBuffer) { 0, 0, 0 }

#define BUFFER_N(buf, n)                                                                                                                                                                               \
  (MinnetBuffer) { ((uint8_t*)(buf)), ((uint8_t*)(buf)) + n, ((uint8_t*)(buf)) + n }

#define buffer_AVAIL(b) ((b)->end - (b)->wrpos)
#define buffer_OFFSET(b) ((b)->wrpos - (b)->start)
#define buffer_SIZE(b) ((b)->end - (b)->start)
#define buffer_START(b) (void*)(b)->start

void buffer_init(struct byte_buffer*, uint8_t* start, size_t len);
struct byte_buffer* buffer_new(JSContext*, size_t size);
BOOL buffer_alloc(struct byte_buffer*, size_t size, JSContext* ctx);
ssize_t buffer_append(struct byte_buffer*, const void* x, size_t n, JSContext* ctx);
void buffer_free(struct byte_buffer*, JSRuntime* rt);
BOOL buffer_write(struct byte_buffer*, const char* x, size_t n);
int buffer_printf(struct byte_buffer*, const char* format, ...);
uint8_t* buffer_realloc(struct byte_buffer*, size_t size, JSContext* ctx);
int buffer_fromarraybuffer(struct byte_buffer*, JSValue value, JSContext* ctx);
int buffer_fromvalue(struct byte_buffer*, JSValue value, JSContext* ctx);
JSValue buffer_tostring(struct byte_buffer const*, JSContext* ctx);
void buffer_finalizer(JSRuntime*, void* opaque, void* ptr);
JSValue buffer_toarraybuffer(struct byte_buffer const*, JSContext* ctx);
void buffer_dump(const char*, struct byte_buffer const* buf);

static inline uint8_t*
buffer_grow(struct byte_buffer* buf, size_t size, JSContext* ctx) {
  return buffer_realloc(buf, (buf->end - buf->start) + size, ctx);
}

#endif /* BUFFER_H */
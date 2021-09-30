#ifndef BUFFER_H
#define BUFFER_H

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <quickjs.h>
#include <cutils.h>
#include <unistd.h>

typedef struct byte_buffer {
  uint8_t *start, *write, *read, *end, *alloc;
} MinnetBuffer;

#define BUFFER(buf) \
  (MinnetBuffer) { ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + sizeof((buf)) - 1, 0 }

#define BUFFER_0() \
  (MinnetBuffer) { 0, 0, 0, 0, 0 }

#define BUFFER_N(buf, n) \
  (MinnetBuffer) { ((uint8_t*)(buf)), ((uint8_t*)(buf)), ((uint8_t*)(buf)) + n, ((uint8_t*)(buf)) + n, 0 }

#define buffer_AVAIL(b) ((b)->end - (b)->write)
#define buffer_WRITE(b) ((b)->write - (b)->start)
#define buffer_REMAIN(b) ((b)->write - (b)->read)
#define buffer_READ(b) ((b)->read - (b)->start)
#define buffer_SIZE(b) ((b)->end - (b)->start)
#define buffer_BEGIN(b) (void*)(b)->start
#define buffer_END(b) (void*)(b)->end

void buffer_init(struct byte_buffer*, uint8_t*, size_t);
BOOL buffer_alloc(struct byte_buffer*, size_t, JSContext*);
ssize_t buffer_append(struct byte_buffer*, const void*, size_t, JSContext*);
void buffer_free(struct byte_buffer*, JSRuntime*);
BOOL buffer_write(struct byte_buffer*, const char*, size_t);
int buffer_vprintf(struct byte_buffer*, const char*, va_list);
int buffer_printf(struct byte_buffer*, const char*, ...);
uint8_t* buffer_realloc(struct byte_buffer*, size_t, JSContext*);
int buffer_fromvalue(struct byte_buffer*, JSValue, JSContext*);
JSValue buffer_tostring(struct byte_buffer const*, JSContext*);
char* buffer_escaped(struct byte_buffer const*, JSContext*);
void buffer_finalizer(JSRuntime*, void*, void*);
JSValue buffer_toarraybuffer(struct byte_buffer const*, JSContext*);
void buffer_dump(const char*, struct byte_buffer const*);

static inline uint8_t*
buffer_grow(struct byte_buffer* buf, size_t size, JSContext* ctx) {
  return buffer_realloc(buf, (buf->end - buf->start) + size, ctx);
}

static inline uint8_t*
buffer_skip(struct byte_buffer* buf, size_t size) {
  assert(buf->read + size <= buf->write);
  buf->read += size;
  return buf->read;
}

static inline BOOL
buffer_putchar(struct byte_buffer* buf, char c) {
  if(buf->write + 1 <= buf->end) {
    *buf->write = (uint8_t)c;
    buf->write++;
    return TRUE;
  }
  return FALSE;
}

#endif /* BUFFER_H */

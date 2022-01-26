#ifndef MINNET_BUFFER_H
#define MINNET_BUFFER_H

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <quickjs.h>
#include <cutils.h>
#include <unistd.h>

struct byte_block {
  uint8_t *start, *end;
};

#define block_SIZE(b) ((b)->end - (b)->start)
#define block_BEGIN(b) (void*)(b)->start
#define block_END(b) (void*)(b)->end
#define block_ALLOC(b) ((b)->start ? (b)->start - LWS_PRE : 0)

void block_init(struct byte_block*, uint8_t*, size_t);
BOOL block_alloc(struct byte_block*, size_t, JSContext*);
uint8_t* block_realloc(struct byte_block*, size_t, JSContext*);
void block_free(struct byte_block*, JSRuntime*);
int block_fromarraybuffer(struct byte_block*, JSValue, JSContext*);
JSValue block_toarraybuffer(struct byte_block*, JSContext*);
JSValue block_tostring(struct byte_block const*, JSContext*);

static inline struct byte_block
block_move(struct byte_block* blk) {
  struct byte_block ret = {blk->start, blk->end};
  blk->start = 0;
  blk->end = 0;
  return ret;
}

typedef struct byte_buffer {
  union {
    struct byte_block block;
    struct {
      uint8_t *start, *end;
    };
  };
  uint8_t *alloc, *read, *write;
} MinnetBuffer;

#define BUFFER(buf) \
  (MinnetBuffer) { ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + sizeof((buf)) - 1, 0 }

#define BUFFER_0() \
  (MinnetBuffer) { 0, 0, 0, 0, 0 }

#define BUFFER_N(buf, n) \
  (MinnetBuffer) { ((uint8_t*)(buf)), ((uint8_t*)(buf)), ((uint8_t*)(buf)), ((uint8_t*)(buf)) + n, 0 }

#define buffer_AVAIL(b) ((b)->end - (b)->write)
#define buffer_WRITE(b) ((b)->write - (b)->start)
#define buffer_REMAIN(b) ((b)->write - (b)->read)
#define buffer_READ(b) ((b)->read - (b)->start)

#define buffer_zero(b) memset((b), 0, sizeof(MinnetBuffer))

void buffer_init(struct byte_buffer*, uint8_t*, size_t);
BOOL buffer_alloc(struct byte_buffer*, size_t, JSContext*);
ssize_t buffer_append(struct byte_buffer*, const void*, size_t, JSContext* ctx);
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

static inline void
buffer_reset(struct byte_buffer* buf) {
  buf->read = buf->start;
  buf->write = buf->start;
}

static inline uint8_t*
buffer_grow(struct byte_buffer* buf, size_t size, JSContext* ctx) {
  return buffer_realloc(buf, (buf->end - buf->start) + size, ctx);
}

static inline BOOL
buffer_clone(struct byte_buffer* buf, const struct byte_buffer* other, JSContext* ctx) {
  if(!buffer_alloc(buf, block_SIZE(other), ctx))
    return FALSE;
  memcpy(buf->start, other->start, buffer_WRITE(other));

  buf->read = buf->start + buffer_READ(other);
  buf->write = buf->start + buffer_WRITE(other);
  return TRUE;
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

#endif /* MINNET_BUFFER_H */

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

typedef struct byte_block {
  uint8_t *start, *end;
} MinnetBytes;
 

#define block_SIZE(b) ((b)->end - (b)->start)
#define block_BEGIN(b) (void*)(b)->start
#define block_END(b) (void*)(b)->end
#define block_ALLOC(b) ((b)->start ? (b)->start - LWS_PRE : 0)

void block_init(MinnetBytes*, uint8_t*, size_t);
uint8_t* block_alloc(MinnetBytes*, size_t, JSContext*);
uint8_t* block_realloc(MinnetBytes*, size_t, JSContext*);
void block_free(MinnetBytes*, JSRuntime*);
int block_fromarraybuffer(MinnetBytes*, JSValue, JSContext*);
JSValue block_toarraybuffer(MinnetBytes*, JSContext*);
JSValue block_tostring(MinnetBytes const*, JSContext*);

static inline uint8_t*
block_grow(MinnetBytes* blk, size_t size, JSContext* ctx) {
  return block_realloc(blk, block_SIZE(blk) + size, ctx);
}

static inline MinnetBytes
block_move(MinnetBytes* blk) {
  MinnetBytes ret = {blk->start, blk->end};
  blk->start = 0;
  blk->end = 0;
  return ret;
}

typedef union byte_buffer {
  struct byte_block;
  struct {
    uint8_t *start, *end;
  };
  struct {
    MinnetBytes block;
    uint8_t *read, *write, *alloc;
  };
} MinnetBuffer;

#define BUFFER(buf) \
  (MinnetBuffer) { ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + sizeof((buf)) - 1, 0 }

#define BUFFER_0() \
  (MinnetBuffer) { 0, 0, 0, 0, 0 }

#define BUFFER_N(buf, n) \
  (MinnetBuffer) { ((uint8_t*)(buf)), ((uint8_t*)(buf)), ((uint8_t*)(buf)), ((uint8_t*)(buf)) + n, 0 }

#define buffer_AVAIL(b) ((b)->end - (b)->write)
#define buffer_BYTES(b) ((b)->write - (b)->read)
#define buffer_HEAD(b) ((b)->write - (b)->start)
#define buffer_TAIL(b) ((b)->read - (b)->start)

#define buffer_zero(b) memset((b), 0, sizeof(MinnetBuffer))

void buffer_init(MinnetBuffer*, uint8_t*, size_t);
uint8_t* buffer_alloc(MinnetBuffer*, size_t, JSContext*);
ssize_t buffer_append(MinnetBuffer*, const void*, size_t, JSContext* ctx);
void buffer_free(MinnetBuffer*, JSRuntime*);
BOOL buffer_write(MinnetBuffer*, const char*, size_t);
int buffer_vprintf(MinnetBuffer*, const char*, va_list);
int buffer_printf(MinnetBuffer*, const char*, ...);
uint8_t* buffer_realloc(MinnetBuffer*, size_t, JSContext*);
int buffer_fromarraybuffer(MinnetBuffer*, JSValue, JSContext*);
int buffer_fromvalue(MinnetBuffer*, JSValue, JSContext*);
JSValue buffer_tostring(MinnetBuffer const*, JSContext*);
char* buffer_escaped(MinnetBuffer const*, JSContext*);
void buffer_finalizer(JSRuntime*, void*, void*);
JSValue  buffer_toarraybuffer(MinnetBuffer*, JSContext*);
void buffer_dump(const char*, MinnetBuffer const*);

static inline void
buffer_reset(MinnetBuffer* buf) {
  buf->read = buf->start;
  buf->write = buf->start;
}

static inline uint8_t*
buffer_grow(MinnetBuffer* buf, size_t size, JSContext* ctx) {
  return block_grow(&buf->block, size, ctx);
}

static inline BOOL
buffer_clone(MinnetBuffer* buf, const MinnetBuffer* other, JSContext* ctx) {
  if(!buffer_alloc(buf, block_SIZE(other), ctx))
    return FALSE;
  memcpy(buf->start, other->start, buffer_HEAD(other));

  buf->read = buf->start + buffer_TAIL(other);
  buf->write = buf->start + buffer_HEAD(other);
  return TRUE;
}

static inline uint8_t*
buffer_skip(MinnetBuffer* buf, size_t size) {
  assert(buf->read + size <= buf->write);
  buf->read += size;
  return buf->read;
}

static inline BOOL
buffer_putchar(MinnetBuffer* buf, char c) {
  if(buf->write + 1 <= buf->end) {
    *buf->write = (uint8_t)c;
    buf->write++;
    return TRUE;
  }
  return FALSE;
}

static inline MinnetBuffer
buffer_move(MinnetBuffer* buf) {
  MinnetBuffer ret = *buf;
  memset(buf, 0, sizeof(MinnetBuffer));
  return ret;
}
#endif /* MINNET_BUFFER_H */

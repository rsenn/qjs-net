#ifndef QJSNET_LIB_BUFFER_H
#define QJSNET_LIB_BUFFER_H

#include <stdint.h>
#include <stdarg.h>
#include <sys/types.h>
#include <quickjs.h>
#include <cutils.h>
#include "jsutils.h"

typedef struct byte_block {
  uint8_t* start;
  uint8_t* end;
} ByteBlock;

#define block_SIZE(b) (size_t)((b)->end - (b)->start)
#define block_BEGIN(b) (void*)(b)->start
#define block_END(b) (void*)(b)->end
#define block_ALLOC(b) (void*)((b)->start ? (b)->start - LWS_PRE : 0)

void block_init(ByteBlock*, uint8_t*, size_t);
uint8_t* block_alloc(ByteBlock*, size_t, JSContext*);
uint8_t* block_realloc(ByteBlock*, size_t, JSContext*);
void block_free(ByteBlock*, JSRuntime*);
int block_fromarraybuffer(ByteBlock*, JSValue, JSContext*);
JSValue block_toarraybuffer(ByteBlock*, JSContext*);
JSValue block_tostring(ByteBlock const*, JSContext*);
static inline ByteBlock
block_fromjs(JSBuffer buf) {
  ByteBlock ret = {buf.data, buf.data + buf.size};
  return ret;
}

static inline uint8_t*
block_grow(ByteBlock* blk, size_t size, JSContext* ctx) {
  return block_realloc(blk, block_SIZE(blk) + size, ctx);
}

static inline ByteBlock
block_move(ByteBlock* blk) {
  ByteBlock ret = {blk->start, blk->end};
  blk->start = 0;
  blk->end = 0;
  return ret;
}

typedef union byte_buffer {
  struct {
    uint8_t *start, *end, *read, *write, *alloc;
  };
  ByteBlock block;
} ByteBuffer;

#define BUFFER(buf) \
  (ByteBuffer) { \
    { (uint8_t*)(buf) + LWS_PRE, (uint8_t*)(buf) + sizeof(buf) - 1, (uint8_t*)(buf) + LWS_PRE, (uint8_t*)(buf) + LWS_PRE, 0 } \
  }

#define BUFFER_0() \
  (ByteBuffer) { \
    { 0, 0, 0, 0, 0 } \
  }

#define BUFFER_N(buf, n) \
  (ByteBuffer) { \
    { (uint8_t*)(buf), (uint8_t*)(buf) + (n), (uint8_t*)(buf), (uint8_t*)(buf), 0 } \
  }

#define buffer_AVAIL(b) (ptrdiff_t)((b)->end - (b)->write)
#define buffer_BYTES(b) (ptrdiff_t)((b)->write - (b)->start)
#define buffer_REMAIN(b) (ptrdiff_t)((b)->write - (b)->read)
#define buffer_HEAD(b) (size_t)((b)->write - (b)->start)
#define buffer_TAIL(b) (size_t)((b)->read - (b)->start)
#define buffer_ALLOC(b) (void*)((b)->alloc)

#define buffer_BEGIN(b) block_BEGIN(&(b)->block)
#define buffer_END(b) block_END(&(b)->block)
#define buffer_SIZE(b) block_SIZE(&(b)->block)

#define buffer_zero(b) memset((b), 0, sizeof(ByteBuffer))

void buffer_init(ByteBuffer*, uint8_t*, size_t);
uint8_t* buffer_alloc(ByteBuffer*, size_t, JSContext*);
ssize_t buffer_append(ByteBuffer*, const void*, size_t, JSContext* ctx);
void buffer_free(ByteBuffer*, JSRuntime*);
BOOL buffer_write(ByteBuffer*, const void*, size_t);
int buffer_vprintf(ByteBuffer*, const char*, va_list);
int buffer_printf(ByteBuffer*, const char*, ...);
uint8_t* buffer_realloc(ByteBuffer*, size_t, JSContext*);
int buffer_fromarraybuffer(ByteBuffer*, JSValueConst, JSContext*);
int buffer_fromvalue(ByteBuffer*, JSValueConst, JSContext*);
JSValue buffer_tostring(ByteBuffer const*, JSContext*);
size_t buffer_escape(ByteBuffer*, const void*, size_t, JSContext* ctx);
char* buffer_escaped(ByteBuffer const*, JSContext*);
void buffer_finalizer(JSRuntime*, void*, void*);
JSValue buffer_toarraybuffer(ByteBuffer*, JSContext*);
JSValue buffer_toarraybuffer_size(ByteBuffer* buf, size_t* sz, JSContext* ctx);
void buffer_dump(const char*, ByteBuffer const*);
BOOL buffer_clone(ByteBuffer*, const ByteBuffer*, JSContext*);
uint8_t* buffer_skip(ByteBuffer*, size_t);
BOOL buffer_putchar(ByteBuffer*, char);
ByteBuffer buffer_move(ByteBuffer*);
uint8_t* buffer_grow(ByteBuffer* buf, size_t size, JSContext* ctx);

static inline void
buffer_reset(ByteBuffer* buf) {
  buf->read = buf->start;
  buf->write = buf->start;
}

/*static inline uint8_t*
buffer_grow(ByteBuffer* buf, size_t size, JSContext* ctx) {
  return block_grow(&buf->block, size, ctx);
}*/

#endif /* QJSNET_LIB_BUFFER_H */

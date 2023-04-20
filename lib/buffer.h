#ifndef QJSNET_LIB_BUFFER_H
#define QJSNET_LIB_BUFFER_H

#include <cutils.h>
#include <libwebsockets.h>
#include <quickjs.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

typedef struct byte_block {
  uint8_t* start;
  uint8_t* end;
} ByteBlock;

#define BLOCK_0() \
  (ByteBlock) { 0, 0 }

#define block_SIZE(b) (size_t)((b)->end - (b)->start)
#define block_BEGIN(b) (void*)(b)->start
#define block_END(b) (void*)(b)->end
#define block_ALLOC(b) (void*)((b)->start ? (b)->start - LWS_PRE : 0)

uint8_t* block_alloc(ByteBlock*, size_t size);
uint8_t* block_realloc(ByteBlock*, size_t size);
void block_free(ByteBlock*);
uint8_t* block_grow(ByteBlock*, size_t size);
ByteBlock block_copy(const void*, size_t size);
JSValue block_toarraybuffer(ByteBlock*, JSContext* ctx);
JSValue block_tostring(ByteBlock*, JSContext* ctx);
ssize_t block_append(ByteBlock*, const void* data, size_t size);

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

uint8_t* buffer_alloc(ByteBuffer*, size_t size);
ssize_t buffer_append(ByteBuffer*, const void* x, size_t n);
void buffer_free(ByteBuffer*);
BOOL buffer_write(ByteBuffer*, const void* x, size_t n);
int buffer_vprintf(ByteBuffer*, const char* format, va_list ap);
int buffer_printf(ByteBuffer*, const char* format, ...);
uint8_t* buffer_realloc(ByteBuffer*, size_t size);
int buffer_fromvalue(ByteBuffer*, JSValueConst value, JSContext* ctx);
JSValue buffer_tostring(ByteBuffer const*, JSContext* ctx);
size_t buffer_escape(ByteBuffer*, const void* x, size_t len);
char* buffer_escaped(ByteBuffer const*);
BOOL buffer_clone(ByteBuffer*, const ByteBuffer* other);
BOOL buffer_putchar(ByteBuffer*, char c);
uint8_t* buffer_grow(ByteBuffer* buf, size_t size);

static inline void
buffer_reset(ByteBuffer* buf) {
  buf->read = buf->start;
  buf->write = buf->start;
}

typedef struct writer {
  uint8_t **write, *end;
} BufferWriter;

static inline BufferWriter
buffer_writer(ByteBuffer* bb) {
  return (BufferWriter){&bb->write, bb->end};
}
typedef struct reader {
  uint8_t **read, *write;
} BufferReader;

static inline BufferReader
buffer_reader(ByteBuffer* bb) {
  return (BufferReader){&bb->read, bb->write};
}

#endif /* QJSNET_LIB_BUFFER_H */

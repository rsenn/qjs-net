#ifndef MINNET_BUFFER_H
#define MINNET_BUFFER_H

#include <stdint.h>
#include <stdarg.h>
#include <sys/types.h>
#include <quickjs.h>
#include <cutils.h>

typedef struct byte_block {
  uint8_t* start;
  uint8_t* end;
} MinnetBytes;

#define block_SIZE(b) (size_t)((b)->end - (b)->start)
#define block_BEGIN(b) (void*)(b)->start
#define block_END(b) (void*)(b)->end
#define block_ALLOC(b) (void*)((b)->start ? (b)->start - LWS_PRE : 0)

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
  struct {
    uint8_t *start, *end, *read, *write, *alloc;
  };
  MinnetBytes block;
} MinnetBuffer;

#define BUFFER(buf) \
  (MinnetBuffer) { \
    { (uint8_t*)(buf) + LWS_PRE, (uint8_t*)(buf) + sizeof(buf) - 1, (uint8_t*)(buf) + LWS_PRE, (uint8_t*)(buf) + LWS_PRE, 0 } \
  }

#define BUFFER_0() \
  (MinnetBuffer) { \
    { 0, 0, 0, 0, 0 } \
  }

#define BUFFER_N(buf, n) \
  (MinnetBuffer) { \
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

#define buffer_zero(b) memset((b), 0, sizeof(MinnetBuffer))

void buffer_init(MinnetBuffer*, uint8_t*, size_t);
uint8_t* buffer_alloc(MinnetBuffer*, size_t, JSContext*);
ssize_t buffer_append(MinnetBuffer*, const void*, size_t, JSContext* ctx);
void buffer_free(MinnetBuffer*, JSRuntime*);
BOOL buffer_write(MinnetBuffer*, const void*, size_t);
int buffer_vprintf(MinnetBuffer*, const char*, va_list);
int buffer_printf(MinnetBuffer*, const char*, ...);
uint8_t* buffer_realloc(MinnetBuffer*, size_t, JSContext*);
int buffer_fromarraybuffer(MinnetBuffer*, JSValueConst, JSContext*);
int buffer_fromvalue(MinnetBuffer*, JSValueConst, JSContext*);
JSValue buffer_tostring(MinnetBuffer const*, JSContext*);
size_t buffer_escape(MinnetBuffer*, const void*, size_t, JSContext* ctx);
char* buffer_escaped(MinnetBuffer const*, JSContext*);
void buffer_finalizer(JSRuntime*, void*, void*);
JSValue buffer_toarraybuffer(MinnetBuffer*, JSContext*);
void buffer_dump(const char*, MinnetBuffer const*);
BOOL buffer_clone(MinnetBuffer*, const MinnetBuffer*, JSContext*);
uint8_t* buffer_skip(MinnetBuffer*, size_t);
BOOL buffer_putchar(MinnetBuffer*, char);
MinnetBuffer buffer_move(MinnetBuffer*);
uint8_t* buffer_grow(MinnetBuffer* buf, size_t size, JSContext* ctx);

static inline void
buffer_reset(MinnetBuffer* buf) {
  buf->read = buf->start;
  buf->write = buf->start;
}

/*static inline uint8_t*
buffer_grow(MinnetBuffer* buf, size_t size, JSContext* ctx) {
  return block_grow(&buf->block, size, ctx);
}*/

#endif /* MINNET_BUFFER_H */

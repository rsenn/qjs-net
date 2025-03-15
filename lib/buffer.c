/**
 * @file buffer.c
 */
#include "buffer.h"
#include "js-utils.h"
#include <assert.h>

uint8_t*
block_alloc(ByteBlock* blk, size_t size) {
  uint8_t* ptr;

  if((ptr = malloc(size + LWS_PRE))) {
    blk->start = ptr + LWS_PRE;
    blk->end = blk->start + size;
  }

  return ptr;
}

uint8_t*
block_realloc(ByteBlock* blk, size_t size) {
  uint8_t* ptr;

  if(!size) {
    block_free(blk);
    return 0;
  }

  if((ptr = realloc(block_ALLOC(blk), size + LWS_PRE))) {
    blk->start = ptr + LWS_PRE;
    blk->end = blk->start + size;
  } else {
    blk->end = blk->start = 0;
  }

  return ptr;
}

void
block_free(ByteBlock* blk) {
  if(blk->start)
    free(blk->start - LWS_PRE);

  blk->start = blk->end = 0;
}

uint8_t*
block_grow(ByteBlock* blk, size_t size) {
  uint8_t* alloc;
  size_t newsize = block_SIZE(blk) + size;

  if((alloc = realloc(block_ALLOC(blk), LWS_PRE + newsize))) {
    blk->start = alloc + LWS_PRE;
    blk->end = blk->start + newsize;
  }

  return alloc ? blk->start : 0;
}

static void
block_finalizer(JSRuntime* rt, void* alloc, void* start) {
  free(alloc);
}

ByteBlock
block_copy(const void* ptr, size_t size) {
  ByteBlock ret = {0, 0};

  if(block_alloc(&ret, size))
    memcpy(ret.start, ptr, size);

  return ret;
}

ByteBlock
block_slice(const ByteBlock* blk, size_t start, size_t end) {
  ByteBlock ret = {0, 0};
  size_t n = block_SIZE(blk);

  if(start > n)
    start = n;

  if(end > n)
    end = n;

  if(end - start == 0)
    return ret;

  if(block_alloc(&ret, end - start))
    memcpy(ret.start, blk->start + start, end - start);

  return ret;
}

JSValue
block_toarraybuffer(ByteBlock* blk, JSContext* ctx) {
  ByteBlock mem = block_move((ByteBlock*)blk);

  return JS_NewArrayBuffer(ctx, block_BEGIN(&mem), block_SIZE(&mem), block_finalizer, block_ALLOC(&mem), FALSE);
}

JSValue
block_tostring(ByteBlock* blk, JSContext* ctx) {
  ByteBlock mem = block_move((ByteBlock*)blk);
  JSValue str = JS_NewStringLen(ctx, block_BEGIN(&mem), block_SIZE(&mem));

  block_free(&mem);

  return str;
}

JSValue
block_tojson(ByteBlock* blk, JSContext* ctx) {
  ByteBlock mem = block_move((ByteBlock*)blk);
  JSValue ret = JS_ParseJSON2(ctx, block_BEGIN(&mem), block_SIZE(&mem), 0, JS_PARSE_JSON_EXT);

  block_free(&mem);

  return ret;
}

ssize_t
block_append(ByteBlock* blk, const void* data, size_t size) {
  size_t offset = block_SIZE(blk);
  uint8_t* start;

  if((start = block_grow(blk, size))) {
    memcpy(start + offset, data, size);
    return size;
  }

  return -1;
}

uint8_t*
buffer_alloc(ByteBuffer* buf, size_t size) {
  uint8_t* ret;

  if((ret = block_alloc(&buf->block, size))) {
    buf->alloc = ret;
    buf->read = buf->start;
    buf->write = buf->start;
  }

  return ret;
}

ssize_t
buffer_append(ByteBuffer* buf, const void* x, size_t n) {
  if(!buffer_BEGIN(buf)) {
    if(!buffer_alloc(buf, n))
      return -1;
  } else {
    if((size_t)buffer_AVAIL(buf) < n + 1)
      if(!buffer_realloc(buf, buffer_HEAD(buf) + n + 1))
        return -1;
  }

  memcpy(buf->write, x, n);
  buf->write[n] = '\0';
  buf->write += n;

  return n;
}

void
buffer_free(ByteBuffer* buf) {
  if(buf->alloc)
    block_free(&buf->block);

  buf->read = buf->write = buf->alloc = 0;
}

BOOL
buffer_write(ByteBuffer* buf, const void* x, size_t n) {
  assert((size_t)buffer_AVAIL(buf) >= n);

  memcpy(buf->write, x, n);
  buf->write += n;

  return TRUE;
}

int
buffer_vprintf(ByteBuffer* buf, const char* format, va_list ap) {
  ssize_t n, size = buffer_AVAIL(buf);
  n = vsnprintf((char*)buf->write, size, format, ap);

  if(n > size)
    return 0;

  if(n >= (int)size)
    n = size;

  buf->write += n;
  return n;
}

int
buffer_puts(ByteBuffer* buf, const char* s) {
  return buffer_append(buf, s, strlen(s));
}

int
buffer_putc(ByteBuffer* buf, int c) {
  char ch = c;

  return buffer_append(buf, &ch, 1);
}

int
buffer_printf(ByteBuffer* buf, const char* format, ...) {
  int n;
  va_list ap;

  va_start(ap, format);
  n = buffer_vprintf(buf, format, ap);
  va_end(ap);

  return n;
}

uint8_t*
buffer_realloc(ByteBuffer* buf, size_t size) {
  size_t rd, wr;
  uint8_t* x;

  if(!size) {
    buffer_free(buf);
    return 0;
  }

  rd = buffer_TAIL(buf);
  wr = buffer_HEAD(buf);
  assert(size >= wr);

  if((x = block_realloc(&buf->block, size))) {
    if(buf->alloc == 0 && buf->start && wr)
      memcpy(x + LWS_PRE, buf->start, wr);

    buf->alloc = x;
    buf->write = buf->start + wr;
    buf->read = buf->start + rd;
  }
  return x;
}

BOOL
buffer_clone(ByteBuffer* buf, const ByteBuffer* other) {
  if(!buffer_alloc(buf, block_SIZE(other)))
    return FALSE;

  memcpy(buf->start, other->start, buffer_HEAD(other));

  buf->read = buf->start + buffer_TAIL(other);
  buf->write = buf->start + buffer_HEAD(other);

  return TRUE;
}

uint8_t*
buffer_grow(ByteBuffer* buf, size_t size) {
  size += buffer_SIZE(buf);

  return buffer_realloc(buf, size);
}

int
buffer_fromvalue(ByteBuffer* buf, JSValueConst value, JSContext* ctx) {
  int ret = -1;
  JSBuffer input = js_input_chars(ctx, value);

  if(input.data == 0 || input.size == 0)
    ret = 0;
  else if(buffer_append(buf, input.data, input.size) == input.size)
    ret = 1;

  js_buffer_free(&input, JS_GetRuntime(ctx));
  return ret;
}

size_t
buffer_avail(ByteBuffer* buf) {
  return buf->end - buf->write;
}

size_t
buffer_remain(ByteBuffer* buf) {
  return buf->write - buf->read;
}

size_t
buffer_bytes(ByteBuffer* buf) {
  return buf->write - buf->start;
}

ssize_t
buffer_read(ByteBuffer* buf, void* x, size_t n) {
  size_t sz = n > buffer_REMAIN(buf) ? buffer_REMAIN(buf) : n;

  if(sz) {
    memcpy(x, buffer_READ(buf), sz);
    buf->read += sz;
  }

  return sz;
}

ssize_t
buffer_gets(ByteBuffer* buf, void* x, size_t n) {
  size_t sz = n > buffer_REMAIN(buf) ? buffer_REMAIN(buf) : n;
  ssize_t sz2 = -1;
  char* ptr;

  if((ptr = memchr(buffer_READ(buf), '\n', buffer_REMAIN(buf)))) {
    size_t pos = ptr + 1 - (char*)buffer_READ(buf);
    sz2 = pos > n ? n : pos;

    memcpy(x, buffer_READ(buf), sz2);

    buf->read += pos;
  }

  return sz2;
}

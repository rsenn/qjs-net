#include "minnet-buffer.h"
#include "jsutils.h"
#include <libwebsockets.h>
#include <assert.h>

void
block_init(MinnetBytes* blk, uint8_t* start, size_t len) {
  blk->start = start;
  blk->end = blk->start + len;
}

uint8_t*
block_alloc(MinnetBytes* blk, size_t size, JSContext* ctx) {
  uint8_t* ptr;

  if((ptr = js_malloc(ctx, size + LWS_PRE))) {
    blk->start = ptr + LWS_PRE;
    blk->end = blk->start + size;
  }

  return ptr;
}

uint8_t*
block_realloc(MinnetBytes* blk, size_t size, JSContext* ctx) {
  uint8_t* ptr;

  if(!size) {
    block_free(blk, JS_GetRuntime(ctx));
    return 0;
  }

  if((ptr = js_realloc(ctx, block_ALLOC(blk), size + LWS_PRE))) {
    blk->start = ptr + LWS_PRE;
    blk->end = blk->start + size;
  } else {
    blk->end = blk->start = 0;
  }

  return ptr;
}

void
block_free(MinnetBytes* blk, JSRuntime* rt) {
  if(blk->start)
    js_free_rt(rt, blk->start - LWS_PRE);

  blk->start = blk->end = 0;
}

static void
block_finalizer(JSRuntime* rt, void* alloc, void* start) {
  js_free_rt(rt, alloc);
}

int
block_fromarraybuffer(MinnetBytes* blk, JSValueConst value, JSContext* ctx) {
  size_t len;

  if(!(blk->start = JS_GetArrayBuffer(ctx, &len, value)))
    return -1;

  blk->end = blk->start + len;
  return 0;
}

JSValue
block_toarraybuffer(MinnetBytes* blk, JSContext* ctx) {
  MinnetBytes mem = block_move(blk);
  return JS_NewArrayBuffer(ctx, block_BEGIN(&mem), block_SIZE(&mem), block_finalizer, block_ALLOC(&mem), FALSE);
}

JSValue
block_tostring(MinnetBytes const* blk, JSContext* ctx) {
  return JS_NewStringLen(ctx, block_BEGIN(blk), block_SIZE(blk));
}

void
buffer_init(MinnetBuffer* buf, uint8_t* start, size_t len) {
  block_init(&buf->block, start, len);

  buf->read = buf->start;
  buf->write = buf->start;
  buf->alloc = 0;
}

uint8_t*
buffer_alloc(MinnetBuffer* buf, size_t size, JSContext* ctx) {
  uint8_t* ret;
  if((ret = block_alloc(&buf->block, size, ctx))) {
    buf->alloc = ret;
    buf->read = buf->start;
    buf->write = buf->start;
  }
  return ret;
}

ssize_t
buffer_append(MinnetBuffer* buf, const void* x, size_t n, JSContext* ctx) {
  if((size_t)buffer_AVAIL(buf) < n) {
    if(!buffer_realloc(buf, buffer_HEAD(buf) + n + 1, ctx))
      return -1;
  }
  memcpy(buf->write, x, n);
  buf->write[n] = '\0';
  buf->write += n;
  return n;
}

void
buffer_free(MinnetBuffer* buf, JSRuntime* rt) {
  if(buf->alloc)
    block_free(&buf->block, rt);
  buf->read = buf->write = buf->alloc = 0;
}

BOOL
buffer_write(MinnetBuffer* buf, const void* x, size_t n) {
  assert((size_t)buffer_AVAIL(buf) >= n);
  memcpy(buf->write, x, n);
  buf->write += n;
  return TRUE;
}

int
buffer_vprintf(MinnetBuffer* buf, const char* format, va_list ap) {
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
buffer_printf(MinnetBuffer* buf, const char* format, ...) {
  int n;
  va_list ap;
  va_start(ap, format);
  n = buffer_vprintf(buf, format, ap);
  va_end(ap);
  return n;
}

uint8_t*
buffer_realloc(MinnetBuffer* buf, size_t size, JSContext* ctx) {
  size_t rd, wr;
  uint8_t* x;

  if(!size) {
    buffer_free(buf, JS_GetRuntime(ctx));
    return 0;
  }

  rd = buffer_TAIL(buf);
  wr = buffer_HEAD(buf);
  assert(size >= wr);

  if((x = block_realloc(&buf->block, size, ctx))) {
    if(buf->alloc == 0 && buf->start && wr)
      memcpy(x + LWS_PRE, buf->start, wr);

    buf->alloc = x;
    buf->write = buf->start + wr;
    buf->read = buf->start + rd;
  }
  return x;
}

int
buffer_fromarraybuffer(MinnetBuffer* buf, JSValueConst value, JSContext* ctx) {
  int ret;

  if(!(ret = block_fromarraybuffer(&buf->block, value, ctx))) {
    buf->read = buf->start;
    buf->write = buf->start;
    buf->alloc = 0;
  }
  return ret;
}

int
buffer_fromvalue(MinnetBuffer* buf, JSValueConst value, JSContext* ctx) {
  int ret = -1;
  JSBuffer input = js_buffer_new(ctx, value);

  if(input.data == 0 || input.size == 0) {
    ret = 0;
  } else if(buffer_append(buf, input.data, input.size, ctx) == input.size) {
    ret = 1;
  }

  js_buffer_free(&input, ctx);
  return ret;
}

JSValue
buffer_tostring(MinnetBuffer const* buf, JSContext* ctx) {
  return JS_NewStringLen(ctx, buf->start, buffer_HEAD(buf));
}

size_t
buffer_escape(MinnetBuffer* buf, const void* x, size_t len, JSContext* ctx) {
  const uint8_t *ptr, *end;

  size_t prev = buffer_REMAIN(buf);

  for(ptr = x, end = (const uint8_t*)x + len; ptr < end; ptr++) {
    char c = *ptr;

    if(buffer_AVAIL(buf) < 4)
      break;

    switch(c) {
      case '\n':
        buffer_putchar(buf, '\\');
        buffer_putchar(buf, 'n');
        break;
      case '\r':
        buffer_putchar(buf, '\\');
        buffer_putchar(buf, 'r');
        break;
      case '\t':
        buffer_putchar(buf, '\\');
        buffer_putchar(buf, 't');
        break;
      case '\v':
        buffer_putchar(buf, '\\');
        buffer_putchar(buf, 'v');
        break;
      case '\b':
        buffer_putchar(buf, '\\');
        buffer_putchar(buf, 'b');
        break;
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 12:
      case 14:
      case 15:
      case 16:
      case 17:
      case 18:
      case 19:
      case 20:
      case 21:
      case 22:
      case 23:
      case 24:
      case 25:
      case 26:
      case 27:
      case 28:
      case 29:
      case 30:
      case 31: buffer_printf(buf, "\\x%02", c); break;
      default: buffer_putchar(buf, c); break;
    }
  }
  return buffer_REMAIN(buf) - prev;
}

char*
buffer_escaped(MinnetBuffer const* buf, JSContext* ctx) {
  char* ptr;
  MinnetBuffer out;
  size_t size = buffer_REMAIN(buf) * 4;

  size = (size + 8) & (~7);

  if(!(ptr = js_malloc(ctx, size)))
    return 0;

  out = BUFFER_N(ptr, size - 1);

  ptr[buffer_escape(&out, buf->read, buffer_REMAIN(buf), ctx)] = '\0';

  return ptr;
}

void
buffer_finalizer(JSRuntime* rt, void* opaque, void* ptr) {
  // MinnetBuffer* buf = opaque;
}

JSValue
buffer_toarraybuffer(MinnetBuffer* buf, JSContext* ctx) {
  MinnetBuffer moved = buffer_move(buf);
  return block_toarraybuffer(&moved.block, ctx);
}

void
buffer_dump(const char* n, MinnetBuffer const* buf) {
  fprintf(stderr, "%s\t{ write = %td, read = %td, size = %td }\n", n, buf->write - buf->start, buf->read - buf->start, buf->end - buf->start);
  fflush(stderr);
}

BOOL
buffer_clone(MinnetBuffer* buf, const MinnetBuffer* other, JSContext* ctx) {
  if(!buffer_alloc(buf, block_SIZE(other), ctx))
    return FALSE;
  memcpy(buf->start, other->start, buffer_HEAD(other));

  buf->read = buf->start + buffer_TAIL(other);
  buf->write = buf->start + buffer_HEAD(other);
  return TRUE;
}

uint8_t*
buffer_skip(MinnetBuffer* buf, size_t size) {
  assert(buf->read + size <= buf->write);
  buf->read += size;
  return buf->read;
}

BOOL
buffer_putchar(MinnetBuffer* buf, char c) {
  if(buf->write + 1 <= buf->end) {
    *buf->write = (uint8_t)c;
    buf->write++;
    return TRUE;
  }
  return FALSE;
}

MinnetBuffer
buffer_move(MinnetBuffer* buf) {
  MinnetBuffer ret = *buf;
  memset(buf, 0, sizeof(MinnetBuffer));
  return ret;
}

uint8_t*
buffer_grow(MinnetBuffer* buf, size_t size, JSContext* ctx) {
  size += buffer_SIZE(buf);
  return buffer_realloc(buf, size, ctx);
}

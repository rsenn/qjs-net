#include "jsutils.h"
#include "minnet-buffer.h"
#include <libwebsockets.h>

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

  buf->read = buf->write = buf->start;
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
buffer_write(MinnetBuffer* buf, const char* x, size_t n) {
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
  JSBuffer input = js_buffer_from(ctx, value);

  if(input.data == 0) {
    ret = 0;
  } else if(buffer_alloc(buf, input.size, ctx)) {
    buffer_write(buf, input.data, input.size);
    ret = 1;
  }
end:
  js_buffer_free(&input, ctx);
  return ret;
}

JSValue
buffer_tostring(MinnetBuffer const* buf, JSContext* ctx) {
  return block_tostring(&buf->block, ctx);
}

char*
buffer_escaped(MinnetBuffer const* buf, JSContext* ctx) {
  MinnetBuffer out = BUFFER_0();
  uint8_t* ptr;

  buffer_alloc(&out, (buffer_HEAD(buf) * 4) + 1, ctx);

  out.start -= LWS_PRE;
  out.read = out.write = out.start;

  for(ptr = buf->start; ptr < buf->write; ptr++) {
    char c = *ptr;
    switch(c) {
      case '\n':
        buffer_putchar(&out, '\\');
        buffer_putchar(&out, 'n');
        break;
      case '\r':
        buffer_putchar(&out, '\\');
        buffer_putchar(&out, 'r');
        break;
      case '\t':
        buffer_putchar(&out, '\\');
        buffer_putchar(&out, 't');
        break;
      case '\v':
        buffer_putchar(&out, '\\');
        buffer_putchar(&out, 'v');
        break;
      case '\b':
        buffer_putchar(&out, '\\');
        buffer_putchar(&out, 'b');
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
      case 31: buffer_printf(&out, "\\x%02", c); break;
      default: buffer_putchar(&out, c); break;
    }
  }
  *out.write = '\0';
  return (char*)out.start;
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

#include "jsutils.h"
#include "buffer.h"
#include <libwebsockets.h>

void
buffer_init(struct byte_buffer* buf, uint8_t* start, size_t len) {
  buf->start = start;
  buf->read = buf->start;
  buf->write = buf->start;
  buf->end = start + len;
  buf->alloc = 0;
}

/*struct byte_buffer*
buffer_new(JSContext* ctx, size_t size) {
  if(size < LWS_RECOMMENDED_MIN_HEADER_SPACE)
    size = LWS_RECOMMENDED_MIN_HEADER_SPACE;
  struct byte_buffer* buf = js_mallocz(ctx, sizeof(struct byte_buffer) + size + LWS_PRE);

  buffer_init(buf, (uint8_t*)&buf[1] + LWS_PRE, size);
  return buf;
}*/

BOOL
buffer_alloc(struct byte_buffer* buf, size_t size, JSContext* ctx) {
  uint8_t* p;
  if((p = js_malloc(ctx, size + LWS_PRE))) {
    buffer_init(buf, p + LWS_PRE, size);
    buf->alloc = p;
    return TRUE;
  }
  return FALSE;
}

ssize_t
buffer_append(struct byte_buffer* buf, const void* x, size_t n, JSContext* ctx) {
  if((size_t)buffer_AVAIL(buf) < n) {
    if(!buffer_realloc(buf, buffer_WRITE(buf) + n + 1, ctx))
      return -1;
  }
  memcpy(buf->write, x, n);
  buf->write[n] = '\0';
  buf->write += n;
  return n;
}

void
buffer_free(struct byte_buffer* buf, JSRuntime* rt) {
  if(buf->alloc)
    js_free_rt(rt, buf->alloc);
  buf->start = 0;
  buf->read = 0;
  buf->write = 0;
  buf->end = 0;
  buf->alloc = 0;
}

BOOL
buffer_write(struct byte_buffer* buf, const char* x, size_t n) {
  assert((size_t)buffer_AVAIL(buf) >= n);
  memcpy(buf->write, x, n);
  buf->write += n;
  return TRUE;
}

int
buffer_vprintf(struct byte_buffer* buf, const char* format, va_list ap) {
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
buffer_printf(struct byte_buffer* buf, const char* format, ...) {
  int n;
  va_list ap;
  va_start(ap, format);
  n = buffer_vprintf(buf, format, ap);
  va_end(ap);
  return n;
}

uint8_t*
buffer_realloc(struct byte_buffer* buf, size_t size, JSContext* ctx) {
  size_t wrofs = buf->write - buf->start;
  size_t rdofs = buf->read - buf->start;
  uint8_t* x;
  assert(size >= wrofs);
  // assert(buf->alloc);

  if(!size) {
    buffer_free(buf, JS_GetRuntime(ctx));
    return 0;
  }

  x = js_realloc(ctx, buf->alloc, size + LWS_PRE);

  if(x) {
    if(buf->alloc == 0 && buf->start && wrofs)
      memcpy(x + LWS_PRE, buf->start, wrofs);

    buf->alloc = x;
    buf->start = x + LWS_PRE;
    buf->write = buf->start + wrofs;
    buf->read = buf->start + rdofs;
    buf->end = buf->start + size;
    return buf->start;
  }
  return 0;
}

/*int
buffer_fromarraybuffer(struct byte_buffer* buf, JSValueConst value, JSContext* ctx) {
  void* ptr;
  size_t len;
  if((ptr = JS_GetArrayBuffer(ctx, &len, value))) {
    buf->start = ptr;
    buf->read = ptr;
    buf->write = ptr;
    buf->end = buf->start + len;
    return 0;
  }
  return 1;
}*/

int
buffer_fromvalue(struct byte_buffer* buf, JSValueConst value, JSContext* ctx) {
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
buffer_tostring(struct byte_buffer const* buf, JSContext* ctx) {
  void* ptr = buf->start;
  size_t len = buf->write - buf->start;
  return JS_NewStringLen(ctx, ptr, len);
}

char*
buffer_escaped(struct byte_buffer const* buf, JSContext* ctx) {
  struct byte_buffer out = BUFFER_0();
  uint8_t* ptr;

  buffer_alloc(&out, (buffer_WRITE(buf) * 4) + 1, ctx);

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
  // struct byte_buffer* buf = opaque;
}

JSValue
buffer_toarraybuffer(struct byte_buffer const* buf, JSContext* ctx) {
  void* ptr = buf->start;
  size_t len = buf->end - buf->start;
  return JS_NewArrayBuffer(ctx, ptr, len, buffer_finalizer, (void*)buf, FALSE);
}

void
buffer_dump(const char* n, struct byte_buffer const* buf) {
  fprintf(stderr, "%s\t{ write = %td, read = %td, size = %td }\n", n, buf->write - buf->start, buf->read - buf->start, buf->end - buf->start);
  fflush(stderr);
}

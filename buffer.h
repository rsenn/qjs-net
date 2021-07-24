#ifndef BUFFER_H
#define BUFFER_H

#include <assert.h>

typedef struct byte_buffer {
  uint8_t *start, *pos, *end;
} MinnetBuffer;

#define BUFFER(buf)                                                                                                                                                                                    \
  (MinnetBuffer) { ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + sizeof((buf)) - 1 }

#define buffer_avail(b) (size_t)((b)->end - (b)->pos)
#define buffer_size(b) (size_t)((b)->pos - (b)->start)
 
static inline void
buffer_init(struct byte_buffer* hdr, uint8_t* start, size_t len) {
  hdr->start = start + LWS_PRE;
  hdr->pos = hdr->start;
  hdr->end = start + len;
}

static inline struct byte_buffer*
buffer_new(JSContext* ctx, size_t size) {
  if(size < LWS_RECOMMENDED_MIN_HEADER_SPACE)
    size = LWS_RECOMMENDED_MIN_HEADER_SPACE;
  size += LWS_PRE;
  struct byte_buffer* hdr = js_mallocz(ctx, sizeof(struct byte_buffer) + size);

  buffer_init(hdr, (uint8_t*)&hdr[1], size);
  return hdr;
}

static inline BOOL
buffer_alloc(struct byte_buffer* hdr, size_t size, JSContext* ctx) {
  uint8_t* p;
  size += LWS_PRE;
  if((p = js_malloc(ctx, size))) {
    buffer_init(hdr, p, size);
    return TRUE;
  }
  return FALSE;
}

static inline BOOL
buffer_append(struct byte_buffer* hdr, const char* x, size_t n) {
  assert(buffer_avail(hdr) >= n);
  memcpy(hdr->pos, x, n);
  hdr->pos[n] = '\0';
  hdr->pos += n;
  return TRUE;
}

static inline int
buffer_printf(struct byte_buffer* hdr, const char* format, ...) {
  va_list ap;
  int n;
  size_t size = lws_ptr_diff_size_t(hdr->end, hdr->pos);

  if(!size)
    return 0;

  va_start(ap, format);
  n = vsnprintf((char*)hdr->pos, size, format, ap);
  va_end(ap);

  if(n >= (int)size)
    n = size;

  hdr->pos += n;

  return n;
}

static inline uint8_t*
buffer_realloc(JSContext* ctx, struct byte_buffer* hdr, size_t size) {
  assert((uint8_t*)&hdr[1] != hdr->start);
  hdr->start = js_realloc(ctx, hdr->start, size);
  hdr->end = hdr->start + size;
  return hdr->start;
}

static inline void
buffer_free(JSContext* ctx, struct byte_buffer* hdr) {
  uint8_t* start = hdr->start;
  if(start == (uint8_t*)&hdr[1]) {
    js_free(ctx, hdr);
  } else {
    js_free(ctx, hdr->start);
    hdr->start = 0;
    hdr->pos = 0;
    hdr->end = 0;
    js_free(ctx, hdr);
  }
}

static inline JSValue
buffer_tostring(JSContext* ctx, struct byte_buffer const* hdr) {
  void* ptr = hdr->start;
  size_t len = hdr->pos - hdr->start;
  return JS_NewStringLen(ctx, ptr, len);
}

static inline void
buffer_finalizer(JSRuntime* rt, void* opaque, void* ptr) {
  struct byte_buffer* hdr = opaque;
}

static inline JSValue
buffer_tobuffer(JSContext* ctx, struct byte_buffer const* hdr) {
  void* ptr = hdr->start;
  size_t len = hdr->end - hdr->start;
  return JS_NewArrayBuffer(ctx, ptr, len, buffer_finalizer, (void*)hdr, FALSE);
}

static inline void
buffer_dump(const char* n, struct byte_buffer const* hdr) {
  printf("\n\t%s\t{ pos = %zx, size = %zx }", n, hdr->pos - hdr->start, hdr->end - hdr->start);
  fflush(stdout);
}


#endif /* BUFFER_H */
#ifndef QJSNET_LIB_HEADERS_H
#define QJSNET_LIB_HEADERS_H

#include <quickjs.h>
#include <libwebsockets.h>
#include "buffer.h"
#include "utils.h"

JSValue headers_object(JSContext*, const void* start, const void* e);
size_t headers_write(ByteBuffer* buffer, struct lws* wsi, uint8_t**, uint8_t* end);
int headers_fromobj(ByteBuffer*, JSValueConst obj, JSContext* ctx);
ssize_t headers_findb(ByteBuffer*, const char* name, size_t namelen);
char* headers_at(ByteBuffer*, size_t* lenptr, size_t index);
char* headers_getlen(ByteBuffer*, size_t* lenptr, const char* name);
char* headers_get(ByteBuffer*, const char* name, JSContext* ctx);
ssize_t headers_find(ByteBuffer*, const char* name);
int headers_tobuffer(JSContext*, ByteBuffer* headers, struct lws* wsi);
char* headers_gettoken(JSContext*, struct lws* wsi, enum lws_token_indexes tok);
ssize_t headers_unsetb(ByteBuffer*, const char* name, size_t namelen);
ssize_t headers_set(ByteBuffer*, const char* name, const char* value);
ssize_t headers_appendb(ByteBuffer*, const char* name, size_t namelen, const char* value, size_t valuelen);

static inline size_t
headers_length(const void* start, const void* end) {
  return byte_chrs(start, (uint8_t*)end - (uint8_t*)start, "\r\n", 2);
}

static inline size_t
headers_next(const void* start, const void* end) {
  return scan_nextline(start, (const uint8_t*)end - (const uint8_t*)start);
}

static inline size_t
headers_namelen(const void* start, const void* end) {
  return byte_chr(start, (uint8_t*)end - (uint8_t*)start, ':');
}

static inline size_t
headers_value(const void* start, const void* end) {
  size_t pos = headers_namelen(start, end), len = (uint8_t*)end - (uint8_t*)start;

  if(pos < len && ((uint8_t*)start)[pos] == ':') {
    ++pos;
    while(pos < len && ((uint8_t*)start)[pos] == ' ') ++pos;
  }
  return pos;
}

size_t headers_size(ByteBuffer* headers);

static inline char*
headers_name(const void* start, const void* end, JSContext* ctx) {
  return js_strndup(ctx, start, headers_namelen(start, end));
}

static inline ssize_t
headers_unset(ByteBuffer* buf, const char* name) {
  return headers_unsetb(buf, name, strlen(name));
}

#endif /* QJSNET_LIB_HEADERS_H */

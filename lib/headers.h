/**
 * @file headers.h
 */
#ifndef QJSNET_LIB_HEADERS_H
#define QJSNET_LIB_HEADERS_H

#include <quickjs.h>
#include <libwebsockets.h>
#include "buffer.h"
#include "utils.h"

JSValue headers_object(JSContext*, const void* start, const void* e);
size_t headers_write(ByteBuffer* buffer, struct lws* wsi, uint8_t**, uint8_t* end);
int headers_fromobj(ByteBuffer*, JSValueConst obj, const char* itemdelim, const char* keydelim, JSContext* ctx);
ssize_t headers_findb(ByteBuffer*, const char* name, size_t namelen, const char* itemdelim);
char* headers_at(ByteBuffer*, size_t* lenptr, size_t index, const char* itemdelim);
char* headers_getlen(ByteBuffer*, size_t* lenptr, const char* name, const char* itemdelim, const char* keydelim);
char* headers_get(ByteBuffer*, const char* name, const char* itemdelim, const char* keydelim, JSContext* ctx);
ssize_t headers_find(ByteBuffer*, const char* name, const char* itemdelim);
int headers_tobuffer(JSContext*, ByteBuffer* headers, struct lws* wsi);
char* headers_gettoken(JSContext*, struct lws* wsi, enum lws_token_indexes tok);
ssize_t headers_unsetb(ByteBuffer*, const char* name, size_t namelen, const char* itemdelim);
ssize_t headers_set(ByteBuffer*, const char* name, const char* value, const char* itemdelim);
ssize_t headers_appendb(ByteBuffer*, const char* name, size_t namelen, const char* value, size_t valuelen, const char* itemdelim);

static inline size_t
headers_length(const void* start, const void* end, const char* itemdelim) {
  return scan_noncharsetnskip(start, itemdelim, (const uint8_t*)end - (const uint8_t*)start);
}

static inline size_t
headers_next(const void* start, const void* end, const char* itemdelim) {
  return scan_past(start, itemdelim, (const uint8_t*)end - (const uint8_t*)start);
}

static inline size_t
headers_namelen(const void* start, const void* end) {
  return scan_charsetnskip(start, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-", end - start);
}

static inline size_t
headers_value(const void* start, const void* end, const char* itemdelim) {
  size_t pos = headers_namelen(start, end), len = (uint8_t*)end - (uint8_t*)start;

  pos += scan_charsetnskip(start + pos, itemdelim, len - pos);
  pos += scan_whitenskip(start + pos, len - pos);

  return pos;
}

size_t headers_size(ByteBuffer* headers, const char* itemdelim);

static inline char*
headers_name(const void* start, const void* end, JSContext* ctx) {
  return js_strndup(ctx, start, headers_namelen(start, end));
}

static inline ssize_t
headers_unset(ByteBuffer* buf, const char* name, const char* itemdelim) {
  return headers_unsetb(buf, name, strlen(name), itemdelim);
}

#endif /* QJSNET_LIB_HEADERS_H */

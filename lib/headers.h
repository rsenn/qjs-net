#ifndef QJSNET_LIB_HEADERS_H
#define QJSNET_LIB_HEADERS_H

#include <quickjs.h>
#include <libwebsockets.h>
#include "buffer.h"

JSValue headers_object(JSContext*, const void* start, const void* e);
char* headers_atom(JSAtom, JSContext* ctx);
int headers_addobj(ByteBuffer*, struct lws* wsi, JSValueConst obj, JSContext* ctx);
size_t headers_write(uint8_t**, uint8_t* end, ByteBuffer* buffer, struct lws* wsi);
int headers_fromobj(ByteBuffer*, JSValueConst obj, JSContext* ctx);
ssize_t headers_set(JSContext*, ByteBuffer* buffer, const char* name, const char* value);
ssize_t headers_findb(ByteBuffer*, const char* name, size_t namelen);
char* headers_at(ByteBuffer*, size_t* lenptr, size_t index);
char* headers_getlen(ByteBuffer*, size_t* lenptr, const char* name);
char* headers_get(ByteBuffer* buffer, const char* name, JSContext* ctx);
ssize_t headers_copy(ByteBuffer*, char* dest, size_t sz, const char* name);
ssize_t headers_find(ByteBuffer*, const char* name);
ssize_t headers_unsetb(ByteBuffer*, const char* name, size_t namelen);
ssize_t headers_unset(ByteBuffer*, const char* name);
int headers_tobuffer(JSContext*, ByteBuffer* headers, struct lws* wsi);
char* headers_gettoken(JSContext*, struct lws* wsi, enum lws_token_indexes tok);
char* headers_tostring(JSContext*, struct lws* wsi);

#endif /* QJSNET_LIB_HEADERS_H */

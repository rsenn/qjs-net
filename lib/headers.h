#ifndef QJSNET_LIB_HEADERS_H
#define QJSNET_LIB_HEADERS_H

#include <quickjs.h>
#include <libwebsockets.h>
#include "buffer.h"

JSValue headers_object(JSContext*, const void*, const void*);
char* headers_atom(JSAtom, JSContext*);
int headers_addobj(ByteBuffer*, struct lws*, JSValueConst, JSContext* ctx);
size_t headers_write(uint8_t**, uint8_t*, ByteBuffer*, struct lws* wsi);
int headers_fromobj(ByteBuffer*, JSValueConst, JSContext*);
ssize_t headers_set(JSContext*, ByteBuffer*, const char*, const char* value);
ssize_t headers_findb(ByteBuffer*, const char*, size_t);
ssize_t headers_find(ByteBuffer*, const char*);
char* headers_at(ByteBuffer* buffer, size_t* lenptr, size_t index);
char* headers_get(ByteBuffer*, size_t*, const char*);
ssize_t headers_copy(ByteBuffer*, char*, size_t, const char* name);
ssize_t headers_unsetb(ByteBuffer*, const char*, size_t);
ssize_t headers_unset(ByteBuffer*, const char*);
int headers_tostring(JSContext*, ByteBuffer*, struct lws*);

#endif /* QJSNET_LIB_HEADERS_H */

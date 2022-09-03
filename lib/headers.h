#ifndef MINNET_HEADERS_H
#define MINNET_HEADERS_H

#include <quickjs.h>
#include <libwebsockets.h>
#include "buffer.h"

JSValue headers_object(JSContext*, const void*, const void*);
char* headers_atom(JSAtom, JSContext*);
int headers_addobj(MinnetBuffer*, struct lws*, JSValueConst, JSContext* ctx);
size_t headers_write(uint8_t**, uint8_t*, MinnetBuffer*, struct lws* wsi);
int headers_fromobj(MinnetBuffer*, JSValueConst, JSContext*);
ssize_t headers_set(JSContext*, MinnetBuffer*, const char*, const char* value);
ssize_t headers_findb(MinnetBuffer*, const char*, size_t);
ssize_t headers_find(MinnetBuffer*, const char*);
char* headers_at(MinnetBuffer* buffer, size_t* lenptr, size_t index);
char* headers_get(MinnetBuffer*, size_t*, const char*);
ssize_t headers_copy(MinnetBuffer*, char*, size_t, const char* name);
ssize_t headers_unsetb(MinnetBuffer*, const char*, size_t);
ssize_t headers_unset(MinnetBuffer*, const char*);
int headers_tostring(JSContext*, MinnetBuffer*, struct lws*);

#endif /* MINNET_HEADERS_H */

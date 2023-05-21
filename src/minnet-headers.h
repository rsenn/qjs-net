#ifndef MINNET_HEADERS_H
#define MINNET_HEADERS_H

#include "headers.h"

typedef void HeadersFreeFunc(void* opaque, JSRuntime* rt);

void* minnet_headers_dup_obj(JSContext*, JSValueConst);
void minnet_headers_free_obj(void*, JSRuntime*);
struct MinnetHeadersOpaque* minnet_headers_opaque(JSValueConst);
ByteBuffer* minnet_headers_data2(JSContext*, JSValueConst);
JSValue minnet_headers_value(JSContext*, ByteBuffer*, JSValueConst);
JSValue minnet_headers_wrap(JSContext*, ByteBuffer*, void*, void (*free_func)(void*, JSRuntime*));
int minnet_headers_init(JSContext*, JSModuleDef*);

extern THREAD_LOCAL JSClassID minnet_headers_class_id;
extern THREAD_LOCAL JSValue minnet_headers_proto, minnet_headers_ctor;

#endif /* MINNET_HEADERS_H */

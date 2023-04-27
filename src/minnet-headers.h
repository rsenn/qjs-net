#ifndef MINNET_HEADERS_H
#define MINNET_HEADERS_H

#include "headers.h"
typedef void HeadersFreeFunc(void* opaque, JSRuntime* rt);

void* minnet_headers_dup_obj(JSContext*, JSValueConst obj);
void minnet_headers_free_obj(void*, JSRuntime* rt);
struct MinnetHeadersOpaque* minnet_headers_opaque(JSValueConst);
ByteBuffer* minnet_headers_data2(JSContext*, JSValueConst obj);
JSValue minnet_headers_value(JSContext*, ByteBuffer* headers, JSValueConst obj);
JSValue minnet_headers_wrap(JSContext* ctx, ByteBuffer* headers, void* opaque, void (*free_func)(void* opaque, JSRuntime* rt));
int minnet_headers_init(JSContext* ctx, JSModuleDef* m);

extern THREAD_LOCAL JSClassID minnet_headers_class_id;
extern THREAD_LOCAL JSValue minnet_headers_proto;

#endif /* MINNET_HEADERS_H */

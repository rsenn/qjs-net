#ifndef MINNET_HEADERS_H
#define MINNET_HEADERS_H

#include "headers.h"
typedef void HeadersFreeFunc(void* opaque, JSRuntime* rt);

void* minnet_headers_dup_obj(JSContext*, JSValueConst obj);
void minnet_headers_free_obj(void*, JSRuntime* rt);
ByteBuffer* minnet_headers_data(JSValueConst);
ByteBuffer* minnet_headers_data2(JSContext*, JSValueConst obj);
JSValue minnet_headers_value(JSContext*, ByteBuffer* headers, JSValueConst obj);
JSValue minnet_headers_wrap(JSContext*, ByteBuffer* headers, void* opaque, void (*free_func)(void*, JSRuntime* rt));
JSValue minnet_headers_new(JSContext*, ByteBuffer* b);
JSValue minnet_headers_method(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[], int magic);
JSValue minnet_headers_iterator(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[], int magic);
JSValue minnet_headers_from(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[]);
JSValue minnet_headers_inspect(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[]);
int minnet_headers_init(JSContext*, JSModuleDef* m);

extern THREAD_LOCAL JSClassID minnet_headers_class_id;
extern THREAD_LOCAL JSValue minnet_headers_proto;

#endif /* MINNET_HEADERS_H */

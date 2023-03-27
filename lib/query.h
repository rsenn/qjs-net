#ifndef QJSNET_LIB_QUERY_H
#define QJSNET_LIB_QUERY_H

#include <quickjs.h>
#include <cutils.h>
#include "jsutils.h"

JSValue query_object(const char* q, JSContext* ctx);
JSValue query_object_len(const char* q, size_t n, JSContext* ctx);
BOOL query_entry(const char* q, size_t n, JSContext* ctx, JSEntry* entry);
char* query_from(JSValueConst obj, JSContext* ctx);

#endif /* QJSNET_LIB_QUERY_H */

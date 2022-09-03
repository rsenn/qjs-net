#ifndef MINNET_QUERY_H
#define MINNET_QUERY_H

#include <quickjs.h>
#include <cutils.h>
#include "jsutils.h"

JSValueConst query_object(const char* q, JSContext* ctx);
JSValueConst query_object_len(const char* q, size_t n, JSContext* ctx);
BOOL query_entry(const char* q, size_t n, JSContext* ctx, JSEntry* entry);
char* query_from(JSValueConst obj, JSContext* ctx);
void minnet_query_entry(char* start, size_t len, JSContext* ctx, JSValueConst obj);

#endif /* MINNET_QUERY_H */

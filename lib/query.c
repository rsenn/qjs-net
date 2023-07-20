/**
 * @file query.c
 */
#include "query.h"

JSValue
query_object(const char* q, JSContext* ctx) {
  return query_object_len(q, strlen(q), ctx);
}

JSValue
query_object_len(const char* q, size_t n, JSContext* ctx) {
  const char *p, *end = q + n;
  size_t entrylen;
  JSValue ret = JS_NewObject(ctx);
  JSEntry entry = JS_ENTRY();

  for(p = q; p < end; p += entrylen + 1) {
    entrylen = byte_chr(p, end - p, '&');
    if(query_entry(p, entrylen, ctx, &entry))
      js_entry_apply(ctx, &entry, ret);
  }
  return ret;
}

BOOL
query_entry(const char* q, size_t n, JSContext* ctx, JSEntry* entry) {
  size_t len;

  if((len = byte_chr(q, n, '=')) < n) {
    char* decoded;
    const char *value = q + len + 1, *end = q + n;

    js_entry_clear(ctx, JS_GetRuntime(entry));

    entry->key = JS_NewAtomLen(ctx, q, len);
    len = end - value;

    decoded = js_strndup(ctx, value, len);
    lws_urldecode(decoded, decoded, len + 1);
    entry->value = JS_NewString(ctx, decoded);
    js_free(ctx, decoded);
    return TRUE;
  }

  return FALSE;
}

char*
query_from(JSValueConst obj, JSContext* ctx) {
  JSPropertyEnum* tab;
  uint32_t tab_len, i;
  DynBuf out;
  dbuf_init2(&out, ctx, (DynBufReallocFunc*)js_realloc);

  if(JS_GetOwnPropertyNames(ctx, &tab, &tab_len, obj, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK))
    return 0;

  for(i = 0; i < tab_len; i++) {
    JSValue value = JS_GetProperty(ctx, obj, tab[i].atom);
    size_t len;
    const char *prop, *str;

    str = JS_ToCStringLen(ctx, &len, value);
    prop = JS_AtomToCString(ctx, tab[i].atom);

    dbuf_putstr(&out, prop);
    dbuf_putc(&out, '=');
    dbuf_realloc(&out, out.size + (len * 3) + 1);

    lws_urlencode((char*)&out.buf[out.size], str, out.allocated_size - out.size);
    out.size += strlen((const char*)&out.buf[out.size]);

    JS_FreeCString(ctx, prop);
    JS_FreeCString(ctx, str);

    JS_FreeValue(ctx, value);
  }

  js_free(ctx, tab);
  return (char*)out.buf;
}

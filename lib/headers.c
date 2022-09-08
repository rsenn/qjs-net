#include "headers.h"
#include <libwebsockets.h>
#include <strings.h>

JSValue
headers_object(JSContext* ctx, const void* start, const void* e) {
  JSValue ret = JS_NewObject(ctx);
  size_t len, namelen, n;
  const uint8_t *x, *end;

  for(x = start, end = e; x < end; x += len + 1) {
    len = byte_chrs(x, end - x, "\r\n", 2);
    if(len > (n = byte_chr(x, len, ':'))) {
      const char* prop = (namelen = n) ? js_strndup(ctx, (const char*)x, namelen) : 0;
      if(x[n] == ':')
        n++;
      if(isspace(x[n]))
        n++;
      if(prop) {
        JS_DefinePropertyValueStr(ctx, ret, prop, JS_NewStringLen(ctx, (const char*)&x[n], len - n), JS_PROP_ENUMERABLE);
        js_free(ctx, (void*)prop);
      }
    }
  }
  return ret;
}

char*
headers_atom(JSAtom atom, JSContext* ctx) {
  char* ret;
  const char* str = JS_AtomToCString(ctx, atom);
  size_t len = strlen(str);

  if((ret = js_malloc(ctx, len + 2))) {
    strcpy(ret, str);
    ret[len] = ':';
    ret[len + 1] = '\0';
  }
  return ret;
}

int
headers_addobj(ByteBuffer* buffer, struct lws* wsi, JSValueConst obj, JSContext* ctx) {
  JSPropertyEnum* tab;
  uint32_t tab_len, i;

  if(JS_GetOwnPropertyNames(ctx, &tab, &tab_len, obj, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK))
    return 0;

  for(i = 0; i < tab_len; i++) {
    JSValue value = JS_GetProperty(ctx, obj, tab[i].atom);
    size_t len;
    void* prop;
    const void* str;
    int ret;

    str = JS_ToCStringLen(ctx, &len, value);
    JS_FreeValue(ctx, value);

    prop = headers_atom(tab[i].atom, ctx);

    ret = lws_add_http_header_by_name(wsi, prop, str, len, &buffer->write, buffer->end);

    js_free(ctx, prop);
    JS_FreeCString(ctx, str);

    if(ret)
      return -1;
  }

  js_free(ctx, tab);
  return 0;
}

size_t
headers_write(uint8_t** in, uint8_t* end, ByteBuffer* buffer, struct lws* wsi) {
  int ret;
  uint8_t *p = buffer->read, *q = buffer->write, *start = *in;

  while(p < q) {
    size_t next = scan_nextline(p, q - p);
    size_t m, namelen = byte_chr(p, next, ':');
    uint8_t* name = p;

    m = namelen + 1;
    while(p[m] && p[m] == ' ') ++m;
    p += m;
    next -= m;

    uint8_t tmp = name[namelen + 1];
    name[namelen + 1] = '\0';
    ret = lws_add_http_header_by_name(wsi, name, p, scan_eol(p, next), in, end);
    name[namelen + 1] = tmp;
    // printf("name=%.*s value='%.*s'\namelen", (int)namelen, name, (int)linelen, p);
    p += next;

    if(ret)
      break;
  }

  return *in - start;
}

int
headers_fromobj(ByteBuffer* buffer, JSValueConst obj, JSContext* ctx) {
  JSPropertyEnum* tab;
  uint32_t tab_len, i;

  if(JS_GetOwnPropertyNames(ctx, &tab, &tab_len, obj, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK))
    return 0;

  for(i = 0; i < tab_len; i++) {
    JSValue jsval = JS_GetProperty(ctx, obj, tab[i].atom);
    size_t value_len, prop_len;
    const char *value, *prop;

    value = JS_ToCStringLen(ctx, &value_len, jsval);
    JS_FreeValue(ctx, jsval);

    prop = JS_AtomToCString(ctx, tab[i].atom);
    prop_len = strlen(prop);

    buffer_grow(buffer, prop_len + 2 + value_len + 2, ctx);

    buffer_write(buffer, prop, prop_len);
    buffer_write(buffer, ": ", 2);
    buffer_write(buffer, value, value_len);
    buffer_write(buffer, "\r\n", 2);

    JS_FreeCString(ctx, prop);
    JS_FreeCString(ctx, value);
  }

  js_free(ctx, tab);
  return i;
}

ssize_t
headers_set(JSContext* ctx, ByteBuffer* buffer, const char* name, const char* value) {
  size_t namelen = strlen(name), valuelen = strlen(value);
  size_t len = namelen + 2 + valuelen + 2;

  buffer_grow(buffer, len, ctx);
  buffer_write(buffer, name, namelen);
  buffer_write(buffer, ": ", 2);
  buffer_write(buffer, value, valuelen);
  buffer_write(buffer, "\r\n", 2);

  return len;
}

ssize_t
headers_findb(ByteBuffer* buffer, const char* name, size_t namelen) {
  uint8_t* ptr;
  ssize_t ret = 0;

  for(ptr = buffer->start; ptr < buffer->write;) {
    size_t len = byte_chrs(ptr, buffer->write - ptr, "\r\n", 2);

    // printf("%s %.*s\n", __func__, (int)len, (char*)ptr);

    if(!strncasecmp((const char*)ptr, name, namelen) && ptr[namelen] == ':')
      return ret;

    while(isspace(ptr[len]) && ptr + len < buffer->write) ++len;
    ptr += len;
    ++ret;
  }

  return -1;
}

char*
headers_at(ByteBuffer* buffer, size_t* lenptr, size_t index) {
  uint8_t* ptr;
  size_t i = 0;
  for(ptr = buffer->start; ptr < buffer->write;) {
    size_t len = byte_chrs(ptr, buffer->write - ptr, "\r\n", 2);
    if(i == index) {
      if(lenptr)
        *lenptr = len;
      return (char*)ptr;
    }
    while(isspace(ptr[len]) && ptr + len < buffer->write) ++len;
    ptr += len;
    ++i;
  }
  return 0;
}

char*
headers_get(ByteBuffer* buffer, size_t* lenptr, const char* name) {
  ssize_t index;

  if((index = headers_find(buffer, name)) != -1) {
    size_t l, n;
    char* ret = headers_at(buffer, &l, index);
    n = scan_nonwhitenskip(ret, l);
    ret += n;
    l -= n;
    n = scan_whitenskip(ret, l);
    ret += n;
    l -= n;
    if(lenptr)
      *lenptr = l;
    return ret;
  }

  return 0;
}

ssize_t
headers_copy(ByteBuffer* buffer, char* dest, size_t sz, const char* name) {
  char* hdr;
  size_t len;

  if((hdr = headers_get(buffer, &len, name))) {
    len = MIN(len, sz);

    strncpy(dest, hdr, len);
    return len;
  }

  return -1;
}

ssize_t
headers_find(ByteBuffer* buffer, const char* name) {
  return headers_findb(buffer, name, strlen(name));
}

ssize_t
headers_unsetb(ByteBuffer* buffer, const char* name, size_t namelen) {
  ssize_t pos;

  if((pos = headers_findb(buffer, name, namelen)) >= 0) {
    uint8_t* ptr = buffer->start + pos;
    size_t len = byte_chrs(ptr, buffer->write - ptr, "\r\n", 2);

    while(isspace(buffer->start[len]) && buffer->start + len < buffer->write) ++len;

    memcpy(ptr, ptr + len, buffer->write - (buffer->start + len));
    buffer->write -= len;

    if(buffer->write < buffer->end)
      memset(buffer->write, 0, buffer->end - buffer->write);
  }
  return pos;
}

ssize_t
headers_unset(ByteBuffer* buffer, const char* name) {
  return headers_unsetb(buffer, name, strlen(name));
}

int
headers_tostring(JSContext* ctx, ByteBuffer* headers, struct lws* wsi) {
  int tok, len, count = 0;

  if(!headers->start)
    buffer_alloc(headers, 1024, ctx);

  for(tok = WSI_TOKEN_HOST; tok < WSI_TOKEN_COUNT; tok++) {
    if(tok == WSI_TOKEN_HTTP || tok == WSI_TOKEN_HTTP_URI_ARGS)
      continue;

    if((len = lws_hdr_total_length(wsi, tok)) > 0) {
      char hdr[len + 1];
      const char* name;

      if((name = (const char*)lws_token_to_string(tok))) {
        int namelen = 1 + byte_chr(name + 1, strlen(name + 1), ':');
        lws_hdr_copy(wsi, hdr, len + 1, tok);
        hdr[len] = '\0';

        // printf("headers %i %.*s '%s'\n", tok, namelen, name, hdr);

        if(!headers->alloc)
          buffer_alloc(headers, 1024, ctx);

        while(!buffer_printf(headers, "%.*s: %s\n", namelen, name, hdr)) { buffer_grow(headers, 1024, ctx); }
        ++count;
      }
    }
  }
  return count;
}

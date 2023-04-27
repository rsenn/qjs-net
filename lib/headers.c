#include "utils.h"
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

size_t
headers_write(ByteBuffer* buffer, struct lws* wsi, uint8_t** in, uint8_t* end) {
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

    buffer_grow(buffer, prop_len + 2 + value_len + 2);

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
headers_findb(ByteBuffer* b, const char* name, size_t namelen, const char* itemdelim) {
  uint8_t* x;
  ssize_t ret = 0;

  for(x = b->start; x < b->write;) {
    size_t c = scan_past(x, itemdelim, b->write - x);
    size_t n = headers_namelen(x, b->write);

    // printf("%s %.*s\n", __func__, (int)c, (char*)x);

    if(n == namelen)
      if(!strncasecmp((const char*)x, name, namelen))
        return ret;

    x += c;
    ++ret;
  }

  return -1;
}

char*
headers_at(ByteBuffer* b, size_t* lenptr, size_t index) {
  uint8_t* x;
  size_t i = 0;
  for(x = b->start; x < b->write;) {
    size_t c = byte_chrs(x, b->write - x, "\r\n", 2);
    if(i == index) {
      if(lenptr)
        *lenptr = c;
      return (char*)x;
    }
    while(isspace(x[c]) && x + c < b->write) ++c;
    x += c;
    ++i;
  }
  return 0;
}

char*
headers_getlen(ByteBuffer* b, size_t* lenptr, const char* name, const char* itemdelim) {
  ssize_t i;

  if((i = headers_find(b, name, itemdelim)) != -1) {
    size_t l, n;
    char* x = headers_at(b, &l, i);
    n = scan_nonwhitenskip(x, l);
    x += n;
    l -= n;
    n = scan_whitenskip(x, l);
    x += n;
    l -= n;
    if(lenptr)
      *lenptr = l;
    return x;
  }

  return 0;
}

char*
headers_get(ByteBuffer* buffer, const char* name, const char* itemdelim, JSContext* ctx) {
  size_t len;
  char* str;

  if((str = headers_getlen(buffer, &len, name, itemdelim)))
    return js_strndup(ctx, str, len);
  return 0;
}

ssize_t
headers_find(ByteBuffer* buffer, const char* name, const char* itemdelim) {
  return headers_findb(buffer, name, strlen(name), itemdelim);
}

int
headers_tobuffer(JSContext* ctx, ByteBuffer* headers, struct lws* wsi) {
  int tok, len, count = 0;

  if(!headers->start)
    buffer_alloc(headers, 1024);

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
          buffer_alloc(headers, 1024);

        while(!buffer_printf(headers, "%.*s: %s\n", namelen, name, hdr)) { buffer_grow(headers, 1024); }
        ++count;
      }
    }
  }
  return count;
}

ssize_t
headers_unsetb(ByteBuffer* b, const char* name, size_t namelen, const char* itemdelim) {
  ssize_t i;

  if((i = headers_findb(b, name, namelen, itemdelim)) >= 0) {
    uint8_t* x = b->start + i;
    size_t c = headers_next(x, b->write, itemdelim);

    if(b->write > x + c)
      memcpy(x, x + c, b->write - (x + c));
    b->write -= c;

    if(b->write < b->end)
      memset(b->write, 0, b->end - b->write);
  }

  return i;
}

ssize_t
headers_set(ByteBuffer* b, const char* name, const char* value, const char* itemdelim) {
  size_t namelen = strlen(name), valuelen = strlen(value);
  size_t c = namelen + 2 + valuelen + 2;

  if(buffer_SIZE(b))
    headers_unsetb(b, name, namelen, itemdelim);

  buffer_grow(b, c);
  buffer_write(b, name, namelen);
  buffer_write(b, ": ", 2);
  buffer_write(b, value, valuelen);
  buffer_write(b, "\r\n", 2);

  return c;
}

ssize_t
headers_appendb(ByteBuffer* b, const char* name, size_t namelen, const char* value, size_t valuelen, const char* itemdelim) {
  ssize_t i;

  buffer_grow(b, valuelen + 2);

  if((i = headers_findb(b, name, namelen, itemdelim)) >= 0) {
    uint8_t *y, *x = buffer_BEGIN(b) + i;
    size_t len  = headers_next(x,  b->write,  itemdelim);

    y = x + len;

    if((b->write - y) > 0 && valuelen > 0) {
      memmove(y + 2 + valuelen, y, b->write - y);
    }
    memcpy(y, ", ", 2);

    if(valuelen > 0)
      memcpy(y + 2, value, valuelen);

    b->write += valuelen + 2;

    if(b->write < b->end)
      memset(b->write, 0, b->end - b->write);
  }

  return i;
}

size_t
headers_size(ByteBuffer* headers, const char* itemdelim) {
  uint8_t *ptr, *end = headers->write;
  size_t i = 0;

  for(ptr = headers->start; ptr != end; ptr += headers_next(ptr, end, itemdelim)) { ++i; }
  return i;
}

/**
 * @file utils.c
 */
#include "utils.h"

size_t
str_chr(const char* in, char needle) {
  const char* t;

  for(t = in; *t; ++t)
    if(*t == needle)
      break;

  return (size_t)(t - in);
}

size_t
str_chrs(const char* in, const char needles[], size_t nn) {
  const char* t = in;
  size_t i;

  for(;;) {
    if(!*t)
      break;

    for(i = 0; i < nn; i++)
      if(*t == needles[i])
        return (size_t)(t - in);

    ++t;
  }

  return (size_t)(t - in);
}

size_t
byte_chr(const void* x, size_t len, char c) {
  const char *s, *t, *str = x;

  for(s = str, t = s + len; s < t; ++s)
    if(*s == c)
      break;

  return s - str;
}

size_t
byte_chrs(const void* x, size_t len, const char needle[], size_t nl) {
  const char *s, *t;

  for(s = x, t = (const char*)x + len; s != t; s++)
    if(byte_chr(needle, nl, *s) < nl)
      break;

  return s - (const char*)x;
}

size_t
byte_rchr(const void* x, size_t len, char needle) {
  const char *s, *t;

  for(s = x, t = (const char*)x + len; --t >= s;)
    if(*t == needle)
      return (size_t)(t - s);

  return len;
}

int
byte_diff(const void* a, size_t len, const void* b) {
  size_t i;

  for(i = 0; i < len; ++i) {
    int r = ((unsigned char*)a)[i] - ((unsigned char*)b)[i];
    if(r)
      return r;
  }

  return 0;
}

size_t
byte_equal(const void* s, size_t n, const void* t) {
  return byte_diff(s, n, t) == 0;
}

size_t
byte_findb(const void* haystack, size_t hlen, const void* what, size_t wlen) {
  size_t i, last;
  const char* s = (const char*)haystack;

  if(hlen < wlen)
    return hlen;

  last = hlen - wlen;

  for(i = 0; i <= last; i++) {
    if(byte_equal(s, wlen, what))
      return i;
    s++;
  }

  return hlen;
}

size_t
scan_whitenskip(const void* s, size_t limit) {
  const char *t = s, *u = t + limit;

  while(t < u && isspace(*t))
    ++t;

  return t - (const char*)s;
}

size_t
scan_nonwhitenskip(const void* s, size_t limit) {
  const char *t = s, *u = t + limit;

  while(t < u && !isspace(*t))
    ++t;

  return t - (const char*)s;
}

size_t
scan_eol(const void* s, size_t limit) {
  const char* t = s;
  size_t i = byte_chr(s, limit, '\n');

  while(i > 0 && t[i - 1] == '\r')
    i--;

  return i;
}

size_t
scan_nextline(const void* s, size_t limit) {
  size_t i;

  if((i = byte_chr(s, limit, '\n')) < limit)
    i++;

  return i;
}

size_t
scan_charsetnskip(const void* s, const char* charset, size_t limit) {
  const char *t, *u, *i;

  for(t = s, u = t + limit; t < u; t++) {
    for(i = charset; *i; i++)
      if(*i == *t)
        break;
    if(*i != *t)
      break;
  }

  return t - (const char*)s;
}

size_t
scan_noncharsetnskip(const void* s, const char* charset, size_t limit) {
  const char *t, *u, *i;

  for(t = s, u = t + limit; t < u; t++) {
    for(i = charset; *i; i++)
      if(*i == *t)
        break;
    if(*i == *t)
      break;
  }

  return t - (const char*)s;
}

size_t
skip_brackets(const char* line, size_t len) {
  size_t n = 0;

  if(len > 0 && line[0] == '[') {
    if((n = byte_chr(line, len, ']')) < len)
      n++;

    while(n < len && isspace(line[n]))
      n++;

    if(n + 1 < len && line[n + 1] == ':')
      n += 2;

    while(n < len && (isspace(line[n]) || line[n] == '-'))
      n++;
  }

  return n;
}

size_t
skip_directory(const char* line, size_t len) {
  if(line[0] == '/') {
    size_t colon = byte_chr(line, len, ':');
    size_t slash = byte_rchr(line, colon, '/');

    if(slash < colon)
      return slash + 1;
  }

  return 0;
}

size_t
strip_trailing_newline(const char* line, size_t* len_p) {
  size_t len = *len_p;

  while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
    len--;

  return *len_p = len;
}

unsigned
uint_pow(unsigned base, unsigned degree) {
  unsigned result = 1;
  unsigned term = base;

  while(degree) {
    if(degree & 1)
      result *= term;
    term *= term;
    degree = degree >> 1;
  }

  return result;
}

size_t
list_size(struct list_head* list) {
  size_t count = 0;

  if(list->next && list->prev) {
    struct list_head* el;
    list_for_each(el, list)++ count;
  }

  return count;
}

struct list_head*
list_at(struct list_head* list, int64_t i) {
  struct list_head* el;

  if(!list_empty(list)) {
    if(i >= 0) {
      list_for_each(el, list) if(i-- == 0) return el;
    } else {
      list_for_each_prev(el, list) if(++i == 0) return el;
    }
  }

  return 0;
}

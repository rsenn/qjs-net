#ifndef MINNET_UTILS_H
#define MINNET_UTILS_H

#include <stddef.h>
#include <ctype.h>

#if defined(_WIN32) || defined(__MINGW32__)
#define VISIBLE __declspec(dllexport)
#define HIDDEN
#else
#define VISIBLE __attribute__((visibility("default")))
#define HIDDEN __attribute__((visibility("hidden")))
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define countof(x) (sizeof(x) / sizeof((x)[0]))
static inline size_t
byte_chr(const void* x, size_t len, char c) {
  const char *s, *t, *str = x;
  for(s = str, t = s + len; s < t; ++s)
    if(*s == c)
      break;
  return s - str;
}

static inline size_t
byte_chrs(const void* x, size_t len, const char needle[], size_t nl) {
  const char *s, *t;
  for(s = x, t = (const char*)x + len; s != t; s++)
    if(byte_chr(needle, nl, *s) < nl)
      break;
  return s - (const char*)x;
}

static inline size_t
byte_rchr(const void* x, size_t len, char needle) {
  const char *s, *t;
  for(s = x, t = (const char*)x + len; --t >= s;) {
    if(*t == needle)
      return (size_t)(t - s);
  }
  return len;
}

static inline size_t
scan_whitenskip(const char* s, size_t limit) {
  const char* t = s;
  const char* u = t + limit;
  while(t < u && isspace(*t)) ++t;
  return (size_t)(t - s);
}

static inline size_t
scan_nonwhitenskip(const char* s, size_t limit) {
  const char* t = s;
  const char* u = t + limit;
  while(t < u && !isspace(*t)) ++t;
  return (size_t)(t - s);
}

static inline size_t
scan_charsetnskip(const char* s, const char* charset, size_t limit) {
  const char* t = s;
  const char* u = t + limit;
  const char* i;
  while(t < u) {
    for(i = charset; *i; ++i)
      if(*i == *t)
        break;
    if(*i != *t)
      break;
    ++t;
  }
  return (size_t)(t - s);
}

static inline unsigned
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

#endif /* MINNET_UTILS_H */

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

  while(t < u && isspace(*t)) ++t;

  return t - (const char*)s;
}

size_t
scan_nonwhitenskip(const void* s, size_t limit) {
  const char *t = s, *u = t + limit;

  while(t < u && !isspace(*t)) ++t;

  return t - (const char*)s;
}

size_t
scan_eol(const void* s, size_t limit) {
  const char* t = s;
  size_t i = byte_chr(s, limit, '\n');

  while(i > 0 && t[i - 1] == '\r') i--;

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

    while(n < len && isspace(line[n])) n++;

    if(n + 1 < len && line[n + 1] == ':')
      n += 2;

    while(n < len && (isspace(line[n]) || line[n] == '-')) n++;
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

  while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;

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

int
socket_geterror(int fd) {
  int e;
  socklen_t sl = sizeof(e);

  if(!getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &sl)) {
    setsockopt(fd, SOL_SOCKET, SO_ERROR, &e, sl);
    return e;
  }

  return -1;
}

char*
socket_address(int fd, int (*fn)(int, struct sockaddr*, socklen_t*)) {
  const char* s = 0;
  union {
    struct sockaddr a;
    struct sockaddr_in ai;
    struct sockaddr_in6 ai6;
  } sa;
  socklen_t sl = sizeof(s);
  uint16_t port;
  static char addr[1024];

  if(fn(fd, &sa.a, &sl) != -1) {
    size_t i;
    s = inet_ntop(sa.ai.sin_family, sa.ai.sin_family == AF_INET ? (void*)&sa.ai.sin_addr : (void*)&sa.ai6.sin6_addr, addr, sizeof(addr));
    i = strlen(s);

    switch(sa.ai.sin_family) {
      case AF_INET: port = ntohs(sa.ai.sin_port); break;
      case AF_INET6: port = ntohs(sa.ai6.sin6_port); break;
    }
    snprintf(&addr[i], sizeof(addr) - i, ":%u", port);
  }

  return (char*)s;
}

void
js_value_dump(JSContext* ctx, const char* n, JSValueConst const* v) {
  const char* str = JS_ToCString(ctx, *v);
  lwsl_user("%s = '%s'\n", n, str);
  JS_FreeCString(ctx, str);
}

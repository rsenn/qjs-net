#ifndef QJSNET_LIB_UTILS_H
#define QJSNET_LIB_UTILS_H

#include <stddef.h>
#include <ctype.h>
#include <libwebsockets.h>
#include <arpa/inet.h>
#include <quickjs.h>
#include <cutils.h>
#include <list.h>

#if defined(_WIN32) || defined(__MINGW32__)
#define VISIBLE __declspec(dllexport)
#define HIDDEN
#define UNUSED
#elif defined(__CLANG__)
#define VISIBLE [[visibility(default)]]
#define HIDDEN [[visibility(hidden)]]
#define UNUSED [[unused]]
#else
#define VISIBLE __attribute__((visibility("default")))
#define HIDDEN __attribute__((visibility("hidden")))
#define UNUSED __attribute__((unused))
#endif

#ifdef _Thread_local
#define THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__SUNPRO_CC) || defined(__IBMCPP__)
#define THREAD_LOCAL __thread
#elif defined(_WIN32)
#define THREAD_LOCAL __declspec(thread)
#else
#error No TLS implementation found.
#endif

#define LOG(name, fmt, args...) \
  lwsl_user("%-25s" \
            " " fmt "\n", \
            (char*)(name), \
            args);

//#define DBG(fmt, args...) LOG(__FILE__, FG("%d") "%-38s" NC " " fmt, (((__LINE__ % 220) + 20) & 0xff), __func__, args)
#define DBG(fmt, args...) lwsl_user("%-25s " fmt, __func__, args);

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define FG(c) "\x1b[38;5;" c "m"
#define BG(c) "\x1b[48;5;" c "m"
#define FGC(c, str) FG(#c) str NC
#define BGC(c, str) BG(#c) str NC
#define NC "\x1b[0m"

#define countof(x) (sizeof(x) / sizeof((x)[0]))

size_t str_chr(const char*, char);
size_t str_chrs(const char*, const char needles[], size_t nn);
size_t byte_chr(const void*, size_t, char);
size_t byte_chrs(const void*, size_t, const char[], size_t);
size_t byte_rchr(const void*, size_t, char);
int byte_diff(const void*, size_t len, const void* b);
size_t byte_equal(const void*, size_t n, const void* t);
size_t byte_findb(const void*, size_t hlen, const void* what, size_t wlen);

static inline char*
str_ndup(const char* s, size_t n) {
  char* r = malloc(n + 1);
  if(r == NULL)
    return NULL;
  memcpy(r, s, n);
  r[n] = '\0';
  return r;
}

static inline size_t
byte_finds(const void* haystack, size_t hlen, const char* what) {
  return byte_findb(haystack, hlen, what, strlen(what));
}

size_t scan_whitenskip(const void*, size_t);
size_t scan_nonwhitenskip(const void*, size_t);
size_t scan_eol(const void*, size_t);
size_t scan_nextline(const void*, size_t);
size_t scan_charsetnskip(const void*, const char*, size_t);
size_t scan_noncharsetnskip(const void*, const char*, size_t);

static inline size_t
scan_past(const void* s, const char* charset, size_t limit) {
  size_t i;

  if((i = scan_noncharsetnskip(s, charset, limit)) < limit) {
    i += scan_charsetnskip(s + i, charset, limit - i);
  }

  return i;
}

size_t skip_brackets(const char*, size_t len);
size_t skip_directory(const char*, size_t len);
size_t strip_trailing_newline(const char*, size_t* len_p);

unsigned uint_pow(unsigned, unsigned degree);

int socket_geterror(int);
char* socket_address(int, int (*fn)(int, struct sockaddr*, socklen_t*));

static inline BOOL
has_query(const char* str) {
  return !!strchr(str, '?');
}

static inline BOOL
has_query_b(const char* str, size_t len) {
  return byte_chr(str, len, '?') < len;
}

static inline char*
socket_remote(int fd) {
  return socket_address(fd, &getpeername);
}

static inline char*
socket_local(int fd) {
  return socket_address(fd, &getsockname);
}

size_t list_size(struct list_head*);
struct list_head* list_at(struct list_head*, int64_t);

static inline struct list_head*
list_front(const struct list_head* list) {
  return list->next != list ? list->next : 0;
}

static inline struct list_head*
list_back(const struct list_head* list) {
  return list->prev != list ? list->prev : 0;
}

#define list_entry_at(list, type, field, index) list_entry(list_at(list, index), type, field)

#endif /* QJSNET_LIB_UTILS_H */

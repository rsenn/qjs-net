#ifndef MINNET_H
#define MINNET_H

#include <cutils.h>
#include <quickjs.h>
#include "jsutils.h"

union byte_buffer;
struct http_request;

#define SETLOG(max_level) lws_set_log_level(((((max_level) << 1) - 1) & (~LLL_PARSER)) | LLL_USER, NULL);

#define ADD(ptr, inst, member) \
  do { \
    (*(ptr)) = (inst); \
    (ptr) = &(*(ptr))->member; \
  } while(0);

#define FG(c) "\x1b[38;5;" c "m"
#define BG(c) "\x1b[48;5;" c "m"
#define FGC(c, str) FG(#c) str NC
#define BGC(c, str) BG(#c) str NC
#define NC "\x1b[0m"

#define LOG(name, fmt, args...) \
  lwsl_user("%-5s" \
            " " fmt "\n", \
            (char*)(name), \
            args);
#define LOGCB(name, fmt, args...) LOG((name), FG("%d") "%-38s" NC " wsi#%" PRId64 " " fmt "", 22 + (reason * 2), lws_callback_name(reason) + 13, opaque ? opaque->serial : -1, args);

/*#include "minnet-buffer.h"
#include "minnet-url.h"*/

enum { READ_HANDLER = 0, WRITE_HANDLER };
enum http_method;

typedef struct lws_pollfd MinnetPollFd;

typedef enum client_state {
  CONNECTING = 0,
  OPEN = 1,
  CLOSING = 2,
  CLOSED = 3,
} MinnetStatus;

typedef enum on_promise {
  ON_RESOLVE = 0,
  ON_REJECT,
} MinnetPromiseEvent;

typedef struct closure {
  int ref_count;
  union {
    struct context* context;
    struct client_context* client;
    struct server_context* server;
  };
  void (*free_func)(/*void**/);
} MinnetClosure;

struct proxy_connection;
struct http_mount;
struct server_context;
struct client_context;

typedef struct context {
  int ref_count;
  JSContext* js;
  struct lws_context* lws;
  struct lws_context_creation_info info;
  BOOL exception;
  JSValue error;
  JSValue crt, key, ca;
  struct TimerClosure* timer;
} MinnetContext;

extern THREAD_LOCAL int32_t minnet_log_level;
extern THREAD_LOCAL JSContext* minnet_log_ctx;
extern THREAD_LOCAL BOOL minnet_exception;
extern THREAD_LOCAL struct list_head minnet_sockets;

void minnet_log_callback(int, const char*);
int socket_geterror(int);
JSValueConst context_exception(MinnetContext*, JSValueConst);
void context_clear(MinnetContext*);
MinnetClosure* closure_new(JSContext*);
MinnetClosure* closure_dup(MinnetClosure*);
void closure_free(void*);
int minnet_lws_unhandled(const char*, int);
void minnet_handlers(JSContext*, struct lws*, struct lws_pollargs, JSValueConst out[2]);
void value_dump(JSContext*, const char*, JSValueConst const*);
JSModuleDef* js_init_module_minnet(JSContext*, const char*);
char* lws_get_peer(struct lws*, JSContext*);
char* lws_get_host(struct lws*, JSContext*);
void lws_peer_cert(struct lws*);
char* fd_address(int, int (*fn)(int, struct sockaddr*, socklen_t*));
char* fd_remote(int);
char* fd_local(int);
int lws_copy_fragment(struct lws*, enum lws_token_indexes, int, DynBuf* db);
int minnet_query_object2(struct lws*, JSContext*, JSValueConst);
void minnet_query_entry(char*, size_t, JSContext*, JSValueConst obj);
int minnet_query_object(struct lws*, JSContext*, JSValueConst);
const char* lws_callback_name(int);

#endif /* MINNET_H */

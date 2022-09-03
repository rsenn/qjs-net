#ifndef MINNET_H
#define MINNET_H

#include <quickjs.h>
#include "jsutils.h"

#define SETLOG(max_level) lws_set_log_level(((((max_level) << 1) - 1) & (~LLL_PARSER)) | LLL_USER, NULL);

#define ADD(ptr, inst, member) \
  do { \
    (*(ptr)) = (inst); \
    (ptr) = &(*(ptr))->member; \
  } while(0);

#define LOG(name, fmt, args...) \
  lwsl_user("%-5s" \
            " " fmt "\n", \
            (char*)(name), \
            args);
#define LOGCB(name, fmt, args...) LOG((name), FG("%d") "%-38s" NC " wsi#%" PRId64 " " fmt "", 22 + (reason * 2), lws_callback_name(reason) + 13, opaque ? opaque->serial : -1, args);

typedef enum socket_state MinnetStatus;

typedef struct closure {
  int ref_count;
  union {
    struct context* context;
    struct client_context* client;
    struct server_context* server;
  };
  void (*free_func)();
} MinnetClosure;

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

extern THREAD_LOCAL struct list_head minnet_sockets;

void minnet_log_callback(int, const char* line);
JSValueConst context_exception(MinnetContext*, JSValueConst retval);
void context_clear(MinnetContext*);
MinnetClosure* closure_new(JSContext*);
MinnetClosure* closure_dup(MinnetClosure*);
void closure_free(void*);
int minnet_lws_unhandled(const char*, int reason);
void minnet_handlers(JSContext*, struct lws* wsi, struct lws_pollargs args, JSValueConst out[2]);
JSModuleDef* js_init_module_minnet(JSContext*, const char* module_name);

#endif /* MINNET_H */

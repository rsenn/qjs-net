#ifndef MINNET_H
#define MINNET_H

#include <inttypes.h>
#include <libwebsockets.h>
#include <quickjs.h>
#include <stddef.h>
#include "js-utils.h"
#include "utils.h"
#include "lws-utils.h"

#include "../lib/poll.h"

#undef DEBUG

#ifdef DEBUG_OUTPUT
#define DEBUG(x...) minnet_debug(x)
#else
#define DEBUG(x...)
#endif

extern struct lws_protocols *minnet_client_protocols, *minnet_server_protocols;

typedef union {
  struct lws_protocols lws;
  struct {
    const char* name;
    lws_callback_function* callback;
    size_t per_session_data_size;
    size_t rx_buffer_size;
    unsigned int id;
    void* user;
    size_t tx_packet_size;
    enum { CLIENT, SERVER } type;
  };
} MinnetProtocols;

#define SETLOG(max_level) lws_set_log_level(((((max_level) << 1) - 1) & (~LLL_PARSER)) | LLL_USER, NULL);

#define ADD(ptr, inst, member) \
  do { \
    (*(ptr)) = (inst); \
    (ptr) = &(*(ptr))->member; \
  } while(0);

#define SKIP(ptr, member) (ptr) = &(*(ptr))->member;

#define ROR(v, n) ((((v) << (8 - n)) | ((v) >> n)) & 0xff)
#define LOGCB(name, fmt, args...) LOG((name), FG("%d") "%-33s" NC " wsi#%" PRId64 " " fmt "", 22 + (ROR(reason, 4) ^ 0), lws_callback_name(reason) + 13, opaque ? opaque->serial : -1, args)

#define STRINGIFY(arg) #arg

#define BOOL_OPTION(name, prop, var) \
  JSValue name = JS_GetPropertyStr(ctx, options, prop); \
  if(!JS_IsUndefined(name)) \
    var = JS_ToBool(ctx, name); \
  JS_FreeValue(ctx, name);

typedef enum socket_state MinnetStatus;
struct js_callback;

void minnet_io_handlers(JSContext*, struct lws*, struct lws_pollargs, JSValueConst[2]);
JSValue minnet_default_fd_callback(JSContext*);
int wsi_handle_poll(struct lws*, enum lws_callback_reasons, struct js_callback*, struct lws_pollargs*);
int minnet_lws_unhandled(const char*, int);

#endif /* MINNET_H */

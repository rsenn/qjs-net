#ifndef MINNET_H
#define MINNET_H

#include <inttypes.h>      // for PRId64
#include <libwebsockets.h> // for LLL_PARSER, LLL_USER, lwsl_...
#include <quickjs.h>       // for JSContext, JSValueConst
#include <stddef.h>        // for NULL
#include "jsutils.h"       // for JS_INIT_MODULE
#include "utils.h"         // for FG, NC

#ifdef DEBUG_OUTPUT
#define DEBUG(x...) printf(x)
#else
#define DEBUG(x...)
#endif

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


#define BOOL_OPTION(name, prop,var) \
  JSValue  name = JS_GetPropertyStr(ctx, options, prop); \
  if(!JS_IsUndefined( name)) \
    var = JS_ToBool(ctx,  name); \
  JS_FreeValue(ctx,  name);

typedef enum socket_state MinnetStatus;

void minnet_io_handlers(JSContext*, struct lws* wsi, struct lws_pollargs args, JSValueConst out[2]);
void minnet_log_callback(int, const char* line);
int minnet_lws_unhandled(const char*, int reason);

struct js_callback;

int wsi_handle_poll(struct lws*, enum lws_callback_reasons, struct js_callback*, struct lws_pollargs* args);
JSValue minnet_get_sessions(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[]);
JSModuleDef* JS_INIT_MODULE(JSContext*, const char* module_name);

#endif /* MINNET_H */

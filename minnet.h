#include "cutils.h"
#include "quickjs.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <libwebsockets.h>

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

#define countof(x) (sizeof(x) / sizeof((x)[0]))
#define JS_CGETSET_MAGIC_FLAGS_DEF(prop_name, fgetter, fsetter, magic_num, flags)                                              \
  {                                                                                                                            \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET_MAGIC, .magic = magic_num, .u = {                       \
      .getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}}                                           \
    }                                                                                                                          \
  }

#define SETLOG lws_set_log_level(LLL_ERR, NULL);

extern JSValue minnet_log, minnet_log_this;
extern JSContext* minnet_log_ctx;
extern BOOL minnet_exception;

JSValue minnet_make_handler(JSContext*, struct lws_pollargs* pfd, struct lws* wsi, int magic);
JSValue minnet_get_log(JSContext*, JSValue this_val);
JSValue minnet_set_log(JSContext*, JSValue this_val, int argc, JSValue argv[]);
JSValue minnet_fetch(JSContext*, JSValue this_val, int argc, JSValue* argv);
void minnet_handlers(JSContext*, struct lws* wsi, struct lws_pollargs* pfd, JSValue out[2]);

enum { READ_HANDLER = 0, WRITE_HANDLER };

static inline void
get_console_log(JSContext* ctx, JSValue* console, JSValue* console_log) {
  JSValue global = JS_GetGlobalObject(ctx);
  *console = JS_GetPropertyStr(ctx, global, "console");
  *console_log = JS_GetPropertyStr(ctx, *console, "log");
  JS_FreeValue(ctx, global);
}

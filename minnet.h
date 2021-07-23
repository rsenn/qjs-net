#ifndef MINNET_H
#define MINNET_H

#include "cutils.h"
#include "quickjs.h"
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

enum { READ_HANDLER = 0, WRITE_HANDLER };

extern JSValue minnet_log, minnet_log_this;
extern JSContext* minnet_log_ctx;
extern BOOL minnet_exception;

void lws_print_unhandled(int);
void minnet_handlers(JSContext*, struct lws* wsi, struct lws_pollargs* pfd, JSValue out[2]);

#endif /* MINNET_H */
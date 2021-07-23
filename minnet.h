#ifndef MINNET_H
#define MINNET_H

#include "cutils.h"
#include "quickjs.h"
#include <libwebsockets.h>

struct http_request;

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

#define countof(x) (sizeof(x) / sizeof((x)[0]))
#define JS_CGETSET_MAGIC_FLAGS_DEF(prop_name, fgetter, fsetter, magic_num, flags)                                                                                                                      \
  {                                                                                                                                                                                                    \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET_MAGIC, .magic = magic_num, .u = {.getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} }               \
  }

#define SETLOG lws_set_log_level(LLL_ERR, NULL);

enum { READ_HANDLER = 0, WRITE_HANDLER };

typedef struct lws_pollfd MinnetPollFd;

typedef struct http_header {
  unsigned char *start, *pos, *end;
} MinnetHttpHeader;

extern JSValue minnet_log, minnet_log_this;
extern JSContext* minnet_log_ctx;
extern BOOL minnet_exception;

extern JSClassID minnet_request_class_id;
void lws_print_unhandled(int);
void minnet_handlers(JSContext*, struct lws* wsi, struct lws_pollargs* args, JSValue out[2]);
char* header_alloc(JSContext*, struct http_header* hdr, size_t size);
char* header_append(JSContext*, struct http_header* hdr, const char* x, size_t n);
char* header_realloc(JSContext*, struct http_header* hdr, size_t size);
void header_free(JSContext*, struct http_header* hdr);
JSValue minnet_request_wrap(JSContext*, struct http_request* req);
JSValue minnet_request_get(JSContext*, JSValue this_val, int magic);
JSValue minnet_request_getter_path(JSContext*, JSValue this_val);
JSModuleDef* js_init_module_minnet(JSContext*, const char* module_name);

#endif /* MINNET_H */
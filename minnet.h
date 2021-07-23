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
#define JS_CGETSET_FLAGS_DEF(prop_name, fgetter, fsetter, flags)                                                                                                                                       \
  {                                                                                                                                                                                                    \
    .name = prop_name, .prop_flags = flags, .def_type = JS_DEF_CGETSET, .u = {.getset = {.get = {.getter_magic = fgetter}, .set = {.setter_magic = fsetter}} }                                         \
  }

#define SETLOG lws_set_log_level(LLL_ERR, NULL);

enum { READ_HANDLER = 0, WRITE_HANDLER };

typedef struct lws_pollfd MinnetPollFd;

extern JSValue minnet_log, minnet_log_this;
extern JSContext* minnet_log_ctx;
extern BOOL minnet_exception;

extern JSClassID minnet_request_class_id;

void lws_print_unhandled(int);
void minnet_handlers(JSContext*, struct lws* wsi, struct lws_pollargs* args, JSValue out[2]);
struct http_header* header_new(JSContext*, size_t size);
void header_init(struct http_header*, uint8_t* start, size_t len);
BOOL header_alloc(JSContext*, struct http_header* hdr, size_t size);
BOOL header_append(JSContext*, struct http_header* hdr, const char* x, size_t n);
char* header_realloc(JSContext*, struct http_header* hdr, size_t size);
void header_free(JSContext*, struct http_header* hdr);
JSValue header_tostring(JSContext*, struct http_header* hdr);
void header_finalizer(JSRuntime*, void* opaque, void* ptr);
JSValue header_tobuffer(JSContext*, struct http_header* hdr);
struct http_request* minnet_request_new(JSContext*, const char* in, struct lws* wsi);
JSValue minnet_request_constructor(JSContext*, const char* in, struct lws* wsi);
JSValue minnet_request_wrap(JSContext*, struct http_request* req);
JSValue minnet_request_get(JSContext*, JSValue this_val, int magic);
JSValue minnet_request_getter_path(JSContext*, JSValue this_val);
JSModuleDef* js_init_module_minnet(JSContext*, const char* module_name);

#endif /* MINNET_H */
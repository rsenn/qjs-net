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

typedef struct byte_buffer {
  uint8_t *start, *pos, *end;
} MinnetBuffer;

typedef struct callback_ws {
  JSContext* ctx;
  JSValueConst* this_obj;
  JSValue* func_obj;
} MinnetCallback;

extern JSValue minnet_log, minnet_log_this;
extern JSContext* minnet_log_ctx;
extern BOOL minnet_exception;

extern JSClassID minnet_request_class_id;

void lws_print_unhandled(int);
void minnet_handlers(JSContext*, struct lws* wsi, struct lws_pollargs* args, JSValue out[2]);
struct byte_buffer* buffer_new(JSContext*, size_t size);
void buffer_init(struct byte_buffer*, uint8_t* start, size_t len);
BOOL buffer_alloc(struct byte_buffer*, size_t size, JSContext* ctx);
BOOL buffer_append(struct byte_buffer*, const char* x, size_t n);
int buffer_printf(struct byte_buffer*, const char* format, ...);
uint8_t* buffer_realloc(JSContext*, struct byte_buffer* hdr, size_t size);
void buffer_free(JSContext*, struct byte_buffer* hdr);
JSValue buffer_tostring(JSContext*, struct byte_buffer const* hdr);
void buffer_finalizer(JSRuntime*, void* opaque, void* ptr);
JSValue buffer_tobuffer(JSContext*, struct byte_buffer const* hdr);
void buffer_dump(const char*, struct byte_buffer const* hdr);
void value_dump(JSContext*, const char* n, JSValue const* v);
JSModuleDef* js_init_module_minnet(JSContext*, const char* module_name);

static inline int
buffer_avail(struct byte_buffer* hdr) {
  return lws_ptr_diff_size_t(hdr->end, hdr->pos);
}

static inline int
buffer_size(struct byte_buffer* hdr) {
  return lws_ptr_diff_size_t(hdr->pos, hdr->start);
}

#define BUFFER(buf)                                                                                                                                                                                    \
  (MinnetBuffer) { ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + LWS_PRE, ((uint8_t*)(buf)) + sizeof((buf)) - 1 }

#endif /* MINNET_H */

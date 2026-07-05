#define _GNU_SOURCE
#include "minnet-server.h"
#include "minnet-client.h"
#include "minnet-request.h"
#include "minnet-response.h"
#include "minnet-websocket.h"
#include "minnet-ringbuffer.h"
#include "minnet-generator.h"
#include "minnet-asynciterator.h"
#include "minnet-formparser.h"
#include "minnet-hash.h"
#include "minnet-fetch.h"
#include "minnet-headers.h"
#include "js-utils.h"
#include "utils.h"
#include "buffer.h"
#include "ssl-utils.h"
#include <libwebsockets.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/time.h>
#include <stdarg.h>

/*#ifdef _WIN32
#include "poll.h"
#endif*/

static THREAD_LOCAL JSValue minnet_log_cb, minnet_log_this;
static THREAD_LOCAL int32_t minnet_log_level = 0;
static THREAD_LOCAL JSContext* minnet_log_ctx = 0;
struct lws_protocols *minnet_client_protocols = 0, *minnet_server_protocols = 0;

#ifndef POLLIN
#define POLLIN 1
#endif
#ifndef POLLOUT
#define POLLOUT 4
#endif
#ifndef POLLERR
#define POLLERR 8
#endif
#ifndef POLLHUP
#define POLLHUP 16
#endif

#define PIO (POLLIN | POLLOUT | POLLERR)

typedef enum { READ_HANDLER = 0, WRITE_HANDLER } JSIOHandler;

#ifdef _WIN32
static THREAD_LOCAL intptr_t* osfhandle_map;
static THREAD_LOCAL size_t osfhandle_count;

static int make_osf_handle(intptr_t handle) {
  int ret;

  assert((HANDLE)handle != INVALID_HANDLE_VALUE);
  ret = _open_osfhandle((SOCKET)handle, 0);

  if(ret >= osfhandle_count) {
    size_t oldsize = osfhandle_count;
    osfhandle_count = ret + 1;
    osfhandle_map = realloc(osfhandle_map, sizeof(intptr_t) * osfhandle_count);
    assert(osfhandle_map);
    memset(&osfhandle_map[oldsize], 0, (osfhandle_count - oldsize) * sizeof(intptr_t));
  }
  osfhandle_map[ret] = handle;

  return ret;
};

static int get_osf_handle(intptr_t handle) {
  assert((HANDLE)handle != INVALID_HANDLE_VALUE);

  if(osfhandle_map) {
    size_t i;
    for(i = 0; i < osfhandle_count; i++)
      if(osfhandle_map[i] == handle)
        return i;
  }

  return make_osf_handle(handle);
}

static void close_osf_handle(int fd) {
  int ret;
  intptr_t handle = (intptr_t)_get_osfhandle(fd);

  assert(fd + 1 <= osfhandle_count);
  assert(osfhandle_map);
  assert((HANDLE)handle != INVALID_HANDLE_VALUE);
  assert(handle == osfhandle_map[fd]);

  close(fd);

  if(osfhandle_count == fd + 1) {
    --osfhandle_count;
    osfhandle_map = realloc(osfhandle_map, sizeof(intptr_t) * osfhandle_count);
  }
}
#else
#define get_osf_handle(fd) (fd)
#endif

typedef struct {
  JSContext* ctx;
  struct lws_context* context;
  /*struct lws* lwsi;*/
  struct pollfd pfd;
} LWSIOHandler;

static void lws_iohandler_free(void* ptr) {
  LWSIOHandler* closure = ptr;
  JSContext* ctx = closure->ctx;

  js_free(ctx, closure);
};

static JSValue lws_iohandler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  LWSIOHandler* closure = ptr;
  struct pollfd x = closure->pfd;
  JSValue ret = JS_UNDEFINED;

  // p->revents = magic & (wr == WRITE_HANDLER ? POLLOUT : POLLIN);

  if(x.revents != x.events) {
    if(poll(&x, 1, 0) < 0)
      lwsl_err("poll error: %s\n", strerror(errno));
  }

  if(x.revents & PIO) {
    struct lws_pollfd y = {x.fd, x.events, x.revents & PIO};

#ifdef _WIN32
    y.fd = (SOCKET)_get_osfhandle(p->fd);
#endif

    if(y.revents & (POLLERR | POLLHUP)) {
      closure->pfd = x;
    }
    /*if(x.revents & POLLOUT)
      if(x.revents & POLLIN)
        x.revents &= ~(POLLOUT);*/
    // errno = 0;

    ret = JS_NewInt32(ctx, lws_service_fd(closure->context, &y));
  }

  return ret;
}

static JSValue minnet_io_handler(JSContext* ctx, int fd, int events, int magic, struct lws* wsi) {
  LWSIOHandler* h;

  if(!(h = js_mallocz(ctx, sizeof(LWSIOHandler))))
    return JS_EXCEPTION;

  *h = (LWSIOHandler){ctx, lws_get_context(wsi), (struct pollfd){fd, events, magic == READ_HANDLER ? POLLIN : POLLOUT}};

  return js_function_cclosure(ctx, lws_iohandler, 0, magic, h, lws_iohandler_free);
}

void minnet_io_handlers(JSContext* ctx, struct lws* wsi, struct lws_pollargs args, JSValue out[2]) {
  int events = args.events & (POLLIN | POLLOUT);

  out[0] = (events & POLLIN) ? minnet_io_handler(ctx, get_osf_handle(args.fd), events, READ_HANDLER, wsi) : JS_NULL;
  out[1] = (events & POLLOUT) ? minnet_io_handler(ctx, get_osf_handle(args.fd), events, WRITE_HANDLER, wsi) : JS_NULL;
}

/*static JSValue
minnet_fd_callback(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue data[]) {
  JSValueConst args[] = {argv[0], JS_NULL};

  args[1] = argv[1];
  JS_Call(ctx, data[0], JS_UNDEFINED, 2, args);

  args[1] = argv[2];
  JS_Call(ctx, data[1], JS_UNDEFINED, 2, args);

  return JS_UNDEFINED;
}*/

struct FDCallbackClosure {
  JSContext* ctx;
  JSValue set_read, set_write;
  JSCFunctionMagic* set_handler;
};

static void minnet_fd_callback_free(void* opaque) {
  struct FDCallbackClosure* closure = opaque;

  JS_FreeValue(closure->ctx, closure->set_read);
  JS_FreeValue(closure->ctx, closure->set_write);
  js_free(closure->ctx, closure);
}

static JSValue minnet_fd_callback_closure(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  struct FDCallbackClosure* closure = opaque;
  JSValueConst args[] = {argv[0], JS_NULL};

  args[1] = argv[1];
  JS_Call(ctx, closure->set_read, JS_UNDEFINED, 2, args);

  args[1] = argv[2];
  JS_Call(ctx, closure->set_write, JS_UNDEFINED, 2, args);

  return JS_UNDEFINED;
}

JSValue minnet_default_fd_callback(JSContext* ctx) {
  JSValue os = js_global_get(ctx, "os");

  if(JS_IsObject(os)) {
    struct FDCallbackClosure* closure;

    if(!(closure = js_malloc(ctx, sizeof(struct FDCallbackClosure))))
      return JS_EXCEPTION;

    closure->ctx = ctx;
    closure->set_read = JS_GetPropertyStr(ctx, os, "setReadHandler");
    closure->set_write = JS_GetPropertyStr(ctx, os, "setWriteHandler");
    closure->set_handler = *((void**)JS_VALUE_GET_OBJ(closure->set_read) + 7);

    return js_function_cclosure(ctx, minnet_fd_callback_closure, 3, 0, closure, minnet_fd_callback_free);
  }

  return JS_ThrowTypeError(ctx, "globalThis.os must be imported module");
}

static void minnet_log_callback(int level, const char* line) {
  if(minnet_log_ctx) {
    size_t n = 0, len = strlen(line);
    const char* x = line;

    if(JS_IsFunction(minnet_log_ctx, minnet_log_cb)) {
      n = skip_brackets(x, len);
      x += n;
      len -= n;
      n = skip_directory(x, len);
      x += n;
      len -= n;

      strip_trailing_newline(x, &len);

      JSValueConst argv[2] = {
          JS_NewInt32(minnet_log_ctx, level),
          JS_NewStringLen(minnet_log_ctx, x, len),
      };
      JSValue ret = JS_Call(minnet_log_ctx, minnet_log_cb, minnet_log_this, 2, argv);

      if(JS_IsException(ret)) {
        JSValue exception = JS_GetException(minnet_log_ctx);
        JS_FreeValue(minnet_log_ctx, exception);
      }

      JS_FreeValue(minnet_log_ctx, argv[0]);
      JS_FreeValue(minnet_log_ctx, argv[1]);
      JS_FreeValue(minnet_log_ctx, ret);
    } else {
      js_console_log(minnet_log_ctx, &minnet_log_this, &minnet_log_cb);
    }
  }
}

static int minnet_pollfds_handle(struct lws* wsi, struct js_callback* cb, struct lws_pollargs args) {
  JSValue argv[3] = {
      JS_NewInt32(cb->ctx, get_osf_handle(args.fd)),
      JS_NULL,
      JS_NULL,
  };

  minnet_io_handlers(cb->ctx, wsi, args, &argv[1]);
  callback_emit(cb, countof(argv), argv);

  js_argv_free(cb->ctx, countof(argv), argv);
  return 0;
}

int minnet_pollfds_change(struct lws* wsi, enum lws_callback_reasons reason, struct js_callback* cb, struct lws_pollargs* args) {

  if(reason != LWS_CALLBACK_LOCK_POLL && reason != LWS_CALLBACK_UNLOCK_POLL)
    LOG("POLL",
        FG("%d") "%-40s" NC " fd=%d events=%s",
        22 + (ROR(reason, 4) ^ 0),
        lws_callback_name(reason) + 13,
        lws_get_socket_fd(wsi),
        args->events == (POLLIN | POLLOUT) ? "IN|OUT"
        : args->events == POLLIN           ? "IN"
        : args->events == POLLOUT          ? "OUT"
                                           : "");

  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: {
      break;
    }

    case LWS_CALLBACK_ADD_POLL_FD:
    case LWS_CALLBACK_DEL_POLL_FD: {
      if(cb->ctx)
        minnet_pollfds_handle(wsi, cb, *args);

      break;
    }

    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      if(cb->ctx)
        // if(args->events != args->prev_events)
        minnet_pollfds_handle(wsi, cb, *args);

      break;
    }

    default: {
      return -1;
    }
  }

  return 0;
}

int minnet_lws_unhandled(const char* handler, int reason) {
  lwsl_warn("Unhandled \x1b[1;31m%s\x1b[0m event: %i %s\n", handler, reason, lws_callback_name(reason));
  assert(0);
  return -1;
}

static JSValue set_log(JSContext* ctx, JSValueConst this_val, JSValueConst value, JSValueConst thisObj) {
  JSValue ret = JS_VALUE_GET_TAG(minnet_log_cb) == 0 ? JS_UNDEFINED : minnet_log_cb;

  minnet_log_ctx = ctx;
  minnet_log_cb = JS_DupValue(ctx, value);

  if(!JS_IsUndefined(minnet_log_this) && !JS_IsNull(minnet_log_this) && JS_VALUE_GET_TAG(minnet_log_this) != 0)
    JS_FreeValue(ctx, minnet_log_this);

  minnet_log_this = JS_DupValue(ctx, thisObj);

  return ret;
}

static JSValue minnet_set_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret;
  if(argc >= 1 && JS_IsNumber(argv[0])) {
    JS_ToInt32(ctx, &minnet_log_level, argv[0]);
    argc--;
    argv++;
  }

  ret = set_log(ctx, this_val, argv[0], argc > 1 ? argv[1] : JS_NULL);
  lws_set_log_level(((unsigned)minnet_log_level & ((1u << LLL_COUNT) - 1)), minnet_log_callback);
  return ret;
}

static EVP_PKEY*
minnet_generate_rsa_key(int bits) {
#if OPENSSL_VERSION_MAJOR >= 3
  return EVP_RSA_gen(bits);
#else
  EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
  EVP_PKEY* pkey = NULL;
  if(!pctx)
    return NULL;
  if(EVP_PKEY_keygen_init(pctx) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, bits) <= 0 || EVP_PKEY_keygen(pctx, &pkey) <= 0) {
    EVP_PKEY_CTX_free(pctx);
    return NULL;
  }
  EVP_PKEY_CTX_free(pctx);
  return pkey;
#endif
}

static int
minnet_is_ip_literal(const char* s) {
  int dots = 0, colons = 0;
  for(const char* p = s; *p; p++) {
    if(*p == '.')
      dots++;
    else if(*p == ':')
      colons++;
    else if(!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')))
      return 0;
  }
  return (dots == 3 && colons == 0) || colons >= 2;
}

static JSValue minnet_generate_cert(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  const char* cn = NULL;
  const char* cn_owned = NULL;
  int bits = 2048, days = 365;
  char** san_list = NULL;
  int san_n = 0;
  JSValue ret = JS_UNDEFINED;
  EVP_PKEY* pkey = NULL;
  X509* cert = NULL;
  BIO* cbio = NULL;
  BIO* kbio = NULL;

  if(argc > 0 && JS_IsObject(argv[0])) {
    JSValue v;

    v = JS_GetPropertyStr(ctx, argv[0], "commonName");
    if(JS_IsString(v)) {
      cn_owned = JS_ToCString(ctx, v);
      cn = cn_owned;
    }
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[0], "bits");
    if(!JS_IsUndefined(v) && !JS_IsNull(v))
      JS_ToInt32(ctx, &bits, v);
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[0], "days");
    if(!JS_IsUndefined(v) && !JS_IsNull(v))
      JS_ToInt32(ctx, &days, v);
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, argv[0], "altNames");
    if(JS_IsArray(ctx, v)) {
      JSValue lenv = JS_GetPropertyStr(ctx, v, "length");
      JS_ToInt32(ctx, &san_n, lenv);
      JS_FreeValue(ctx, lenv);
      if(san_n > 0) {
        san_list = calloc(san_n, sizeof(char*));
        for(int i = 0; i < san_n; i++) {
          JSValue item = JS_GetPropertyUint32(ctx, v, i);
          const char* s = JS_ToCString(ctx, item);
          san_list[i] = s ? strdup(s) : NULL;
          if(s)
            JS_FreeCString(ctx, s);
          JS_FreeValue(ctx, item);
        }
      }
    }
    JS_FreeValue(ctx, v);
  }

  if(!cn)
    cn = "localhost";
  if(bits < 512)
    bits = 2048;
  if(days <= 0)
    days = 365;

  if(!(pkey = minnet_generate_rsa_key(bits))) {
    ret = JS_ThrowInternalError(ctx, "generateCert: RSA key generation failed");
    goto out;
  }

  if(!(cert = X509_new())) {
    ret = JS_ThrowInternalError(ctx, "generateCert: X509_new failed");
    goto out;
  }

  ASN1_INTEGER_set(X509_get_serialNumber(cert), (long)time(NULL));
  X509_gmtime_adj(X509_get_notBefore(cert), 0);
  X509_gmtime_adj(X509_get_notAfter(cert), (long)days * 86400L);
  X509_set_version(cert, 2);
  X509_set_pubkey(cert, pkey);

  {
    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char*)cn, -1, -1, 0);
    X509_set_issuer_name(cert, name);
  }

  {
    X509V3_CTX v3ctx;
    X509V3_set_ctx_nodb(&v3ctx);
    X509V3_set_ctx(&v3ctx, cert, cert, NULL, NULL, 0);

    X509_EXTENSION* ext;
    if((ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_basic_constraints, "critical,CA:TRUE"))) {
      X509_add_ext(cert, ext, -1);
      X509_EXTENSION_free(ext);
    }
    if((ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_key_usage, "digitalSignature,keyEncipherment,keyCertSign"))) {
      X509_add_ext(cert, ext, -1);
      X509_EXTENSION_free(ext);
    }

    /* Build SAN entries: default DNS:<cn> if none provided. */
    char* san_str = NULL;
    size_t san_cap = 0, san_len = 0;
#define SAN_APPEND(prefix, value) \
  do { \
    size_t need = strlen(prefix) + strlen(value) + (san_len ? 1 : 0) + 1; \
    if(san_len + need > san_cap) { \
      san_cap = (san_len + need) * 2; \
      san_str = realloc(san_str, san_cap); \
    } \
    if(san_len) \
      san_str[san_len++] = ','; \
    memcpy(san_str + san_len, prefix, strlen(prefix)); \
    san_len += strlen(prefix); \
    memcpy(san_str + san_len, value, strlen(value)); \
    san_len += strlen(value); \
    san_str[san_len] = '\0'; \
  } while(0)

    if(san_n > 0) {
      for(int i = 0; i < san_n; i++) {
        if(!san_list[i])
          continue;
        SAN_APPEND(minnet_is_ip_literal(san_list[i]) ? "IP:" : "DNS:", san_list[i]);
      }
    } else {
      SAN_APPEND(minnet_is_ip_literal(cn) ? "IP:" : "DNS:", cn);
    }
#undef SAN_APPEND

    if(san_str) {
      if((ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_subject_alt_name, san_str))) {
        X509_add_ext(cert, ext, -1);
        X509_EXTENSION_free(ext);
      }
      free(san_str);
    }
  }

  if(!X509_sign(cert, pkey, EVP_sha256())) {
    ret = JS_ThrowInternalError(ctx, "generateCert: X509_sign failed");
    goto out;
  }

  cbio = ssl_bio_dynbuf_new();
  if(!PEM_write_bio_X509(cbio, cert)) {
    ret = JS_ThrowInternalError(ctx, "generateCert: PEM cert write failed");
    goto out;
  }
  JSValue cert_ab = ssl_bio_dynbuf_jsarraybuffer(cbio, ctx);

  kbio = ssl_bio_dynbuf_new();
  if(!PEM_write_bio_PrivateKey(kbio, pkey, NULL, NULL, 0, NULL, NULL)) {
    JS_FreeValue(ctx, cert_ab);
    ret = JS_ThrowInternalError(ctx, "generateCert: PEM key write failed");
    goto out;
  }
  JSValue key_ab = ssl_bio_dynbuf_jsarraybuffer(kbio, ctx);

  ret = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, ret, "cert", cert_ab);
  JS_SetPropertyStr(ctx, ret, "key", key_ab);
  JS_SetPropertyStr(ctx, ret, "commonName", JS_NewString(ctx, cn));

out:
  if(cbio)
    BIO_free(cbio);
  if(kbio)
    BIO_free(kbio);
  if(cert)
    X509_free(cert);
  if(pkey)
    EVP_PKEY_free(pkey);
  if(cn_owned)
    JS_FreeCString(ctx, cn_owned);
  if(san_list) {
    for(int i = 0; i < san_n; i++)
      free(san_list[i]);
    free(san_list);
  }
  return ret;
}

static JSValue minnet_get_sessions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  struct list_head* el;
  JSValue ret;
  uint32_t i = 0;

  ret = JS_NewArray(ctx);

  if(opaque_list.prev == NULL)
    init_list_head(&opaque_list);

  list_for_each_prev(el, &opaque_list) {
    struct wsi_opaque_user_data* opaque = list_entry(el, struct wsi_opaque_user_data, link);

#ifdef DEBUG_OUTPUT
    lwsl_user("DEBUG                    %-22s @%u #%" PRId64 " %p", __func__, i, opaque->serial, opaque);
#endif

    JS_SetPropertyUint32(ctx, ret, i++, opaque->sess ? session_object(opaque->sess, ctx) : opaque->ws ? minnet_ws_wrap(ctx, opaque->ws) : JS_NewInt64(ctx, opaque->serial));
  }
  return ret;
}

static const JSCFunctionListEntry minnet_loglevels[] = {
    JS_INDEX_STRING_DEF(1, "ERR"),
    JS_INDEX_STRING_DEF(2, "WARN"),
    JS_INDEX_STRING_DEF(4, "NOTICE"),
    JS_INDEX_STRING_DEF(8, "INFO"),
    JS_INDEX_STRING_DEF(16, "DEBUG"),
    JS_INDEX_STRING_DEF(32, "PARSER"),
    JS_INDEX_STRING_DEF(64, "HEADER"),
    JS_INDEX_STRING_DEF(128, "EXT"),
    JS_INDEX_STRING_DEF(256, "CLIENT"),
    JS_INDEX_STRING_DEF(512, "LATENCY"),
    JS_INDEX_STRING_DEF(1024, "USER"),
    JS_INDEX_STRING_DEF(2048, "THREAD"),
    JS_INDEX_STRING_DEF(4095, "ALL"),
};

static const JSCFunctionListEntry minnet_funcs[] = {
    JS_CFUNC_DEF("createServer", 1, minnet_server),
    JS_CFUNC_DEF("client", 1, minnet_client),
    JS_CFUNC_DEF("fetch", 1, minnet_fetch),
    JS_CFUNC_DEF("getSessions", 0, minnet_get_sessions),
    JS_CFUNC_DEF("setLog", 1, minnet_set_log),
    JS_CFUNC_DEF("generateCert", 1, minnet_generate_cert),
    JS_PROP_INT32_DEF("METHOD_GET", METHOD_GET, 0),
    JS_PROP_INT32_DEF("METHOD_POST", METHOD_POST, 0),
    JS_PROP_INT32_DEF("METHOD_OPTIONS", METHOD_OPTIONS, 0),
    JS_PROP_INT32_DEF("METHOD_PUT", METHOD_PUT, 0),
    JS_PROP_INT32_DEF("METHOD_PATCH", METHOD_PATCH, 0),
    JS_PROP_INT32_DEF("METHOD_DELETE", METHOD_DELETE, 0),
    JS_PROP_INT32_DEF("METHOD_HEAD", METHOD_HEAD, 0),

    JS_PROP_INT32_DEF("LLL_ERR", LLL_ERR, 0),
    JS_PROP_INT32_DEF("LLL_WARN", LLL_WARN, 0),
    JS_PROP_INT32_DEF("LLL_NOTICE", LLL_NOTICE, 0),
    JS_PROP_INT32_DEF("LLL_INFO", LLL_INFO, 0),
    JS_PROP_INT32_DEF("LLL_DEBUG", LLL_DEBUG, 0),
    JS_PROP_INT32_DEF("LLL_PARSER", LLL_PARSER, 0),
    JS_PROP_INT32_DEF("LLL_HEADER", LLL_HEADER, 0),
    JS_PROP_INT32_DEF("LLL_EXT", LLL_EXT, 0),
    JS_PROP_INT32_DEF("LLL_CLIENT", LLL_CLIENT, 0),
    JS_PROP_INT32_DEF("LLL_LATENCY", LLL_LATENCY, 0),
    JS_PROP_INT32_DEF("LLL_USER", LLL_USER, 0),
    JS_PROP_INT32_DEF("LLL_THREAD", LLL_THREAD, 0),
    JS_PROP_INT32_DEF("LLL_ALL", ~((~0u) << LLL_COUNT), 0),

    JS_PROP_INT32_DEF("LWS_WRITE_TEXT", LWS_WRITE_TEXT, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_BINARY", LWS_WRITE_BINARY, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_CONTINUATION", LWS_WRITE_CONTINUATION, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_HTTP", LWS_WRITE_HTTP, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_PING", LWS_WRITE_PING, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_PONG", LWS_WRITE_PONG, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_HTTP_FINAL", LWS_WRITE_HTTP_FINAL, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_HTTP_HEADERS", LWS_WRITE_HTTP_HEADERS, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_HTTP_HEADERS_CONTINUATION", LWS_WRITE_HTTP_HEADERS_CONTINUATION, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_BUFLIST", LWS_WRITE_BUFLIST, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_NO_FIN", LWS_WRITE_NO_FIN, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_H2_STREAM_END", LWS_WRITE_H2_STREAM_END, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_CLIENT_IGNORE_XOR_MASK", LWS_WRITE_CLIENT_IGNORE_XOR_MASK, 0),
    JS_PROP_INT32_DEF("LWS_WRITE_RAW", LWS_WRITE_RAW, 0),

    JS_OBJECT_DEF("logLevels", minnet_loglevels, countof(minnet_loglevels), JS_PROP_CONFIGURABLE),
};

static int js_minnet_init(JSContext* ctx, JSModuleDef* m) {

  // minnet_js_module = JS_ReadObject(ctx, qjsc_minnet, qjsc_minnet_size, JS_READ_OBJ_BYTECODE);

  JS_SetModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));

  minnet_response_init(ctx, m);
  minnet_request_init(ctx, m);
  minnet_ringbuffer_init(ctx, m);
  minnet_generator_init(ctx, m);
  minnet_ws_init(ctx, m);
  minnet_formparser_init(ctx, m);
  minnet_hash_init(ctx, m);
  minnet_asynciterator_init(ctx, m);
  minnet_url_init(ctx, m);
  minnet_headers_init(ctx, m);
  minnet_client_init(ctx, m);
  minnet_server_init(ctx, m);

  return 0;
}

UNUSED VISIBLE JSModuleDef* JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if(!(m = JS_NewCModule(ctx, module_name, js_minnet_init)))
    return NULL;

  JS_AddModuleExport(ctx, m, "Response");
  JS_AddModuleExport(ctx, m, "Request");
  JS_AddModuleExport(ctx, m, "Ringbuffer");
  JS_AddModuleExport(ctx, m, "Generator");
  JS_AddModuleExport(ctx, m, "Socket");
  JS_AddModuleExport(ctx, m, "FormParser");
  JS_AddModuleExport(ctx, m, "Hash");
  JS_AddModuleExport(ctx, m, "AsyncIterator");
  JS_AddModuleExport(ctx, m, "URL");
  JS_AddModuleExport(ctx, m, "Headers");
  JS_AddModuleExport(ctx, m, "Client");
  JS_AddModuleExport(ctx, m, "Server");

  JS_AddModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));

  minnet_log_ctx = ctx;

  lws_set_log_level(minnet_log_level, minnet_log_callback);

  return m;
}

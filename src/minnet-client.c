#define _GNU_SOURCE
#include "minnet-client-http.h"
#include "minnet-client.h"
#include "minnet-websocket.h"
#include "minnet-request.h"
#include "minnet-response.h"
#include "minnet-asynciterator.h"
#include "minnet-generator.h"
#include "context.h"
#include "closure.h"
#include "minnet.h"
#include "js-utils.h"
#include <quickjs-libc.h>
#include <string.h>
#include <errno.h>
#include <libwebsockets.h>

THREAD_LOCAL JSValue minnet_client_proto, minnet_client_ctor;
THREAD_LOCAL JSClassID minnet_client_class_id;

// static JSCallback client_cb_message, client_cb_connect, client_cb_close, client_cb_pong, client_cb_fd;

static int client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

static THREAD_LOCAL struct list_head minnet_clients = {0, 0};

static const struct lws_protocols client_protocols[] = {
    {"raw", client_callback, 0, 0, 0, 0, 0},
    {"http", http_client_callback, 0, 0, 0, 0, 0},
    {"ws", client_callback, 0, 0, 0, 0, 0},
    LWS_PROTOCOL_LIST_TERM,
};

static JSValue
close_status(JSContext* ctx, const char* in, size_t len) {
  if(len >= 2)
    return JS_NewInt32(ctx, ((uint8_t*)in)[0] << 8 | ((uint8_t*)in)[1]);
  return JS_UNDEFINED;
}

static JSValue
close_reason(JSContext* ctx, const char* in, size_t len) {
  if(len > 2)
    return JS_NewStringLen(ctx, &in[2], len - 2);
  return JS_UNDEFINED;
}

void
client_certificate(struct context* context, JSValueConst options) {
  struct lws_context_creation_info* info = &context->info;
  JSContext* ctx = context->js;

  context->crt = JS_GetPropertyStr(ctx, options, "sslCert");
  context->key = JS_GetPropertyStr(ctx, options, "sslPrivateKey");
  context->ca = JS_GetPropertyStr(ctx, options, "sslCA");

  if(JS_IsString(context->crt))
    info->client_ssl_cert_filepath = js_tostring(ctx, context->crt);
  else if(JS_IsObject(context->crt))
    info->client_ssl_cert_mem = js_toptrsize(ctx, &info->client_ssl_cert_mem_len, context->crt);

  if(JS_IsString(context->key))
    info->client_ssl_private_key_filepath = js_tostring(ctx, context->key);
  else if(JS_IsObject(context->key))
    info->client_ssl_key_mem = js_toptrsize(ctx, &info->client_ssl_key_mem_len, context->key);

  if(JS_IsString(context->ca))
    info->client_ssl_ca_filepath = js_tostring(ctx, context->ca);
  else if(JS_IsObject(context->ca))
    info->client_ssl_ca_mem = js_toptrsize(ctx, &info->client_ssl_ca_mem_len, context->ca);
}

MinnetClient*
client_new(JSContext* ctx) {
  MinnetClient* client;

  if(!(client = js_mallocz(ctx, sizeof(MinnetClient))))
    return 0;

  client_zero(client);

  if(minnet_clients.next == NULL)
    init_list_head(&minnet_clients);

  /*  list_add_tail(&client->link, &minnet_clients);*/
  context_add(&client->context);

  client->gen = NULL;

  return client;
}
/*
void
client_free(MinnetClient* client, JSContext* ctx) {
  return client_free(client, JS_GetRuntime(ctx));
}*/

void
client_free(MinnetClient* client, JSRuntime* rt) {
  if(--client->ref_count == 0) {
    DEBUG("%s() client=%p\n", __func__, client);

    // if(client->link.prev) list_del(&client->link);

    /*JS_FreeValueRT(rt, client->headers);
    client->headers = JS_UNDEFINED;*/
    JS_FreeValueRT(rt, client->body);
    client->body = JS_UNDEFINED;
    JS_FreeValueRT(rt, client->next);
    client->next = JS_UNDEFINED;

    if(client->connect_info.method) {
      js_free_rt(rt, (void*)client->connect_info.method);
      client->connect_info.method = 0;
    }

    js_async_free(rt, &client->promise);

    context_clear(&client->context);
    context_delete(&client->context);

    session_clear(&client->session, rt);

    /*    if(--client->iter.ref_count == 0)
          asynciterator_clear(&client->iter, rt);*/
    if(client->gen) {
      generator_free(client->gen);
      client->gen = 0;
    }

    js_free_rt(rt, client);
  }
}

void
client_zero(MinnetClient* client) {
  client->ref_count = 1;
  /*client->headers = JS_NULL;*/
  client->body = JS_NULL;
  client->next = JS_NULL;

  session_init(&client->session, 0);
  js_async_zero(&client->promise);
  callbacks_zero(&client->on);

  client->iter = 0;

  if(client->gen)
    generator_free(client->gen);
  client->gen = 0;

  // asynciterator_zero(&client->iter);
}

MinnetClient*
client_dup(MinnetClient* client) {
  ++client->ref_count;
  return client;
}

Generator*
client_generator(MinnetClient* client, JSContext* ctx) {
  if(!client->gen)
    client->gen = generator_new(ctx);

  return client->gen;
}

struct client_context*
lws_client(struct lws* wsi) {
  return lws_context_user(lws_get_context(wsi));
}

static int
client_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetClient* client = lws_client(wsi);
  struct wsi_opaque_user_data* opaque = 0;
  JSContext* ctx = 0;
  int ret = 0;

  if(lws_reason_poll(reason))
    return wsi_handle_poll(wsi, reason, &client->on.fd, in);

  if(lws_reason_http(reason))
    return http_client_callback(wsi, reason, user, in, len);

  if((ctx = client->context.js))
    opaque = lws_opaque(wsi, ctx);

  LOGCB("CLIENT      ", "fd=%d h2=%i tls=%i len=%zu%s%.*s%s", lws_get_socket_fd(wsi), wsi_http2(wsi), wsi_tls(wsi), len, (in && len) ? " in='" : "", (int)len, (char*)in, (in && len) ? "'" : "");

  switch(reason) {
    case LWS_CALLBACK_GET_THREAD_ID: {
      return gettid();
    }

    case LWS_CALLBACK_VHOST_CERT_AGING:
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
      return 0;
    }

    case LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION: {
      return 0;
    }

    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
    case LWS_CALLBACK_PROTOCOL_INIT: {
      break;
    }

    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: {
      break;
    }

    case LWS_CALLBACK_WS_CLIENT_BIND_PROTOCOL:
    case LWS_CALLBACK_RAW_SKT_BIND_PROTOCOL: {
      session_init(&client->session, wsi_context(wsi));
      break;
    }

    case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL:
    case LWS_CALLBACK_RAW_SKT_DROP_PROTOCOL: {

      break;
    }

    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
    case LWS_CALLBACK_CONNECTING: {
      break;
    }

    case LWS_CALLBACK_WSI_CREATE:
    case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: {
      if(!opaque->ws)
        opaque->ws = ws_new(wsi, ctx);
      break;
    }

    case LWS_CALLBACK_CLIENT_CLOSED: {
      if(opaque->status < CLOSED) {
        opaque->status = CLOSED;
      }
    }

    case LWS_CALLBACK_RAW_CLOSE:
    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      int32_t result = -1, err = -1;

      if(reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR && in) {
        if(!strncmp("conn fail: ", in, 11)) {
          err = /*opaque->error =*/atoi(&((const char*)in)[11]);
          client->context.error = JS_NewString(client->context.js, strerror(err));
        } else {
          client->context.error = JS_NewStringLen(client->context.js, in, len);
        }
      } else
        client->context.error = JS_UNDEFINED;

      opaque->status = CLOSING;

      if(client->iter) {
        if(reason != LWS_CALLBACK_CLIENT_CONNECTION_ERROR)
          if(asynciterator_emplace(client->iter, JS_NULL, TRUE, ctx))
            return 0;

        if(asynciterator_throw(client->iter, client->context.error, ctx))
          return 0;
      }

      if(js_async_pending(&client->promise)) {
        js_async_reject(ctx, &client->promise, client->context.error);
      }

      if(client->on.close.ctx) {
        JSValue ret;
        int argc = 1;
        JSValueConst argv[4] = {client->session.ws_obj};

        if(reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR) {
          argv[argc++] = JS_UNDEFINED;
          argv[argc++] = JS_DupValue(ctx, client->context.error);
        } else {
          argv[argc++] = close_status(ctx, in, len);
          argv[argc++] = close_reason(ctx, in, len);
        }
        argv[argc++] = err != -1 ? JS_NewInt32(ctx, err) : JS_UNDEFINED;

        ret = client_exception(client, callback_emit(&client->on.close, argc, argv));

        if(JS_IsNumber(ret))
          JS_ToInt32(ctx, &result, ret);

        JS_FreeValue(ctx, ret);

        while(--argc >= 0)
          JS_FreeValue(ctx, argv[argc]);
      }

      ret = result;
      break;
    }

    case LWS_CALLBACK_RAW_ADOPT: {
      // lwsl_user("ADOPT! %s", client->request->url.protocol);

      if(strcasecmp(client->request->url.protocol, "udp"))
        break;
    }

    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
    case LWS_CALLBACK_RAW_CONNECTED: {

      opaque->status = OPEN;

      if(js_async_pending(&client->promise)) {
        JSValue cli = minnet_client_wrap(ctx, client_dup(client));

        js_async_resolve(ctx, &client->promise, cli);

        JS_FreeValue(ctx, cli);
      }

      if(client->on.connect.ctx) {
        if(reason != LWS_CALLBACK_RAW_CONNECTED)
          client->session.req_obj = minnet_request_wrap(ctx, client->request);

        client_exception(client, callback_emit(&client->on.connect, 3, &client->session.ws_obj));
      }
      /*if(!minnet_response_data(sess->resp_obj))*/
      break;
    }

    case LWS_CALLBACK_CLIENT_WRITEABLE:
    case LWS_CALLBACK_RAW_WRITEABLE: {
      if(client->on.writeable.ctx) {
        JSValue ret;
        // opaque->callback = WRITEABLE;
        ret = client_exception(client, callback_emit(&client->on.writeable, 1, &client->session.ws_obj));

        if(JS_IsBool(ret)) {
          if(JS_ToBool(ctx, ret) == FALSE) {
            client->on.writeable = CALLBACK_INIT(0, JS_NULL, JS_NULL);
          }
        }
        // opaque->callback = -1;

        if(client->on.writeable.ctx)
          lws_callback_on_writable(wsi);
        return 0;
      }

      /*
      ByteBuffer* buf = &client->session.send_buf;
      int ret, size = buffer_REMAIN(buf);

      if((ret = lws_write(wsi, buf->read, size, LWS_WRITE_TEXT)) != size) {
        lwsl_err("sending message failed: %d < %d\n", ret, size);
        ret = -1;
        break;
      }

      buffer_reset(buf);*/

      session_writable(&client->session, wsi, ctx);
      break;
    }

    case LWS_CALLBACK_RECEIVE:
    case LWS_CALLBACK_CLIENT_RECEIVE:
    case LWS_CALLBACK_RAW_RX: {
      BOOL first = lws_is_first_fragment(wsi), final = lws_is_final_fragment(wsi);
      BOOL single_fragment = first && final;

      if(!single_fragment) {
        if(!client->recvq)
          client->recvq = queue_new(ctx);

        QueueItem* it = first ? queue_write(client->recvq, in, len, ctx) : queue_append(client->recvq, in, len, ctx);

        it->binary = lws_frame_is_binary(wsi);
      }

      if(final) {
        JSValue msg;

        if(!single_fragment) {
          BOOL done = FALSE, binary = FALSE;
          ByteBlock blk = queue_next(client->recvq, &done, &binary);
          msg = binary ? block_toarraybuffer(&blk, ctx) : block_tostring(&blk, ctx);
        } else {
          msg = lws_frame_is_binary(wsi) ? JS_NewArrayBufferCopy(ctx, in, len) : JS_NewStringLen(ctx, in, len);
        }

        if(client->iter) {
          if(asynciterator_yield(client->iter, msg, ctx))
            return 0;
        }

        if(client->on.message.ctx) {
          JSValue argv[] = {
              client->session.ws_obj,
              msg,
          };

          client_exception(client, callback_emit(&client->on.message, countof(argv), argv));

          JS_FreeValue(client->on.message.ctx, argv[1]);
        }
      }

      break;
    }

    case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
      JSContext* ctx;
      if((ctx = client->on.pong.ctx)) {
        JSValue data = JS_NewArrayBufferCopy(ctx, in, len);
        JSValue cb_argv[] = {client->session.ws_obj, data};
        client_exception(client, callback_emit(&client->on.pong, 2, cb_argv));
        JS_FreeValue(ctx, cb_argv[1]);
      }
      break;
    }

    case LWS_CALLBACK_WSI_DESTROY: {
      if(client->wsi == wsi) {
        BOOL is_error = JS_IsUndefined(client->context.error);

        (is_error ? js_async_reject : js_async_resolve)(client->context.js, &client->promise, client->context.error);
        JS_FreeValue(client->context.js, client->context.error);
        client->context.error = JS_UNDEFINED;
      }

      break;
    }

    case LWS_CALLBACK_PROTOCOL_DESTROY: {
      break;
    }
    default: {
      minnet_lws_unhandled(__func__, reason);
      break;
    }
  }

  if(opaque && opaque->status >= CLOSING)
    ret = -1;

  /*  lwsl_user(len ? "client      " FG("%d") "%-38s" NC " is_ssl=%i len=%zu in='%.*s' ret=%d\n" : "client      " FG("%d") "%-38s" NC " is_ssl=%i len=%zu\n",
              22 + (reason * 2),
              lws_callback_name(reason) + 13,
              wsi_tls(wsi),
              len,
              (int)MIN(len, 32),
              (reason == LWS_CALLBACK_RAW_RX || reason == LWS_CALLBACK_CLIENT_RECEIVE || reason == LWS_CALLBACK_RECEIVE) ? 0 : (char*)in,
              ret);*/

  return ret;
}

enum {
  CLIENT_ASYNCITERATOR,
  CLIENT_ITERATOR,
};

typedef struct {
  JSValue msg;
  JSContext* ctx;
} MessageClosure;

static void
message_closure_free(void* ptr) {
  MessageClosure* closure = ptr;
  JSContext* ctx = closure->ctx;
  js_free(ctx, closure);
}

static MessageClosure*
message_closure_new(JSContext* ctx) {
  MessageClosure* closure;

  if((closure = js_malloc(ctx, sizeof(MessageClosure)))) {
    closure->ctx = ctx;
    closure->msg = JS_UNDEFINED;
  }

  return closure;
}

static JSValue
minnet_client_onmessage(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  MessageClosure* closure = opaque;

  closure->msg = JS_DupValue(ctx, argv[0]);
  return JS_UNDEFINED;
}

static JSValue
minnet_client_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  MinnetClient* client;
  JSValue ret = JS_UNDEFINED;

  if(!(client = minnet_client_data2(ctx, this_val)))
    return JS_EXCEPTION;

  client->next = JS_UNDEFINED;

  JSValue oldhandler, onmessage = js_function_cclosure(ctx, minnet_client_onmessage, 0, 0, message_closure_new(ctx), message_closure_free);

  oldhandler = client->on.message.func_obj;

  client->on.message.func_obj = onmessage;
  client->on.message.ctx = ctx;

  for(;;) {
    js_std_loop(ctx);

    if(!JS_IsUndefined(client->next)) {
      ret = client->next;
      client->next = JS_UNDEFINED;
      break;
    }
  }

  client->on.message.func_obj = oldhandler;
  client->on.message.ctx = JS_IsNull(oldhandler) ? 0 : ctx;

  JS_FreeValue(ctx, onmessage);

  return ret;
}

static JSValue
minnet_client_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  MinnetClient* client;
  JSValue ret = JS_UNDEFINED;

  if(!(client = minnet_client_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case CLIENT_ASYNCITERATOR: {
      Generator* gen = client_generator(client, ctx);
      ret = minnet_generator_iterator(ctx, gen);

      break;
    }

    case CLIENT_ITERATOR: {
      ret = JS_NewObject(ctx);

      JS_SetPropertyStr(ctx, ret, "next", js_function_bind_this(ctx, JS_NewCFunction(ctx, &minnet_client_next, "next", 0), this_val));

      break;
    }
  }

  return ret;
}
enum {
  CLIENT_REQUEST,
  CLIENT_RESPONSE,
  CLIENT_SOCKET,
  CLIENT_ONMESSAGE,
  CLIENT_ONCONNECT,
  CLIENT_ONCLOSE,
  CLIENT_ONPONG,
  CLIENT_ONFD,
  CLIENT_ONHTTP,
  CLIENT_ONREAD,
  CLIENT_ONPOST,
  CLIENT_ONWRITEABLE,
};

static JSValue
minnet_client_get(JSContext* ctx, JSValueConst this_val, int magic) {
  MinnetClient* client;
  JSValue ret = JS_UNDEFINED;

  if(!(client = minnet_client_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case CLIENT_REQUEST: {
      ret = JS_DupValue(ctx, client->session.req_obj);
      break;
    }

    case CLIENT_RESPONSE: {
      ret = JS_DupValue(ctx, client->session.resp_obj);
      break;
    }

    case CLIENT_SOCKET: {
      ret = JS_DupValue(ctx, client->session.ws_obj);
      break;
    }

    case CLIENT_ONMESSAGE:
    case CLIENT_ONCONNECT:
    case CLIENT_ONCLOSE:
    case CLIENT_ONPONG:
    case CLIENT_ONFD:
    case CLIENT_ONHTTP:
    case CLIENT_ONREAD:
    case CLIENT_ONPOST:
    case CLIENT_ONWRITEABLE: {
      ret = JS_DupValue(ctx, client->on.cb[magic - CLIENT_ONMESSAGE].func_obj);
      break;
    }
  }
  return ret;
}

static JSValue
minnet_client_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  MinnetClient* client;
  JSValue ret = JS_UNDEFINED;

  if(!(client = minnet_client_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case CLIENT_ONMESSAGE:
    case CLIENT_ONCONNECT:
    case CLIENT_ONCLOSE:
    case CLIENT_ONPONG:
    case CLIENT_ONFD:
    case CLIENT_ONHTTP:
    case CLIENT_ONREAD:
    case CLIENT_ONPOST:
    case CLIENT_ONWRITEABLE: {
      BOOL disabled = JS_IsNull(value);

      client->on.cb[magic - CLIENT_ONMESSAGE] = CALLBACK_INIT(disabled ? 0 : ctx, JS_DupValue(ctx, value), JS_NULL);

      if(magic == CLIENT_ONWRITEABLE)
        if(!disabled)
          lws_callback_on_writable(client->wsi);

      break;
    }
  }
  return ret;
}

JSValue
minnet_client_response(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, JSValue func_data[]) {
  /*JS_DefinePropertyValueStr(ctx, argv[1], "ws", JS_DupValue(ctx, this_val), 0);

  if(!js_is_nullish(argv[0]))
    JS_DefinePropertyValueStr(ctx, argv[1], "request", JS_DupValue(ctx, argv[0]), 0);*/

  JSValue ret = JS_Call(ctx, func_data[0], JS_UNDEFINED, 1, &argv[1]);

  JS_FreeValue(ctx, ret);
  return JS_NewInt32(ctx, 1);
}

typedef struct {
  size_t nfds;
  struct pollfd* pfds;
  int ref_count;
  BOOL touched;
  JSValue exception;
} SyncFetch;

static SyncFetch*
synchfetch_dup(SyncFetch* c) {
  ++c->ref_count;
  return c;
}

static void
synchfetch_free(void* ptr) {
  SyncFetch* c = ptr;

  if(--c->ref_count == 0) {
    memset(c, 0, sizeof(SyncFetch));
    free(c);
  }
}

static ssize_t
synchfetch_indexpollfd(SyncFetch* c, int fd) {
  size_t i;

  for(i = 0; i < c->nfds; i++)
    if(c->pfds[i].fd == fd)
      return i;

  return -1;
}

static struct pollfd*
synchfetch_getpollfd(SyncFetch* c, int fd) {
  ssize_t i;

  if((i = synchfetch_indexpollfd(c, fd)) == -1)
    return 0;

  return &c->pfds[i];
}

static void
synchfetch_setevents(SyncFetch* c, int fd, int events) {
  struct pollfd* pfd;

  if(!(pfd = synchfetch_getpollfd(c, fd))) {
    c->pfds = realloc(c->pfds, (c->nfds + 1) * sizeof(struct pollfd));
    assert(c->pfds);
    c->pfds[c->nfds] = (struct pollfd){fd, 0, 0};
    pfd = &c->pfds[c->nfds++];
  }

  if(pfd->events != events) {
    pfd->events = events;
    c->touched = TRUE;
  }
}

static void
synchfetch_removefd(SyncFetch* c, int fd) {
  ssize_t i = synchfetch_indexpollfd(c, fd);

  assert(i != -1);

  size_t n = (c->nfds - (i + 1));

  if(n)
    memcpy(&c->pfds[i], &c->pfds[i + 1], n * sizeof(struct pollfd));

  --c->nfds;
  c->touched = TRUE;
}

JSValue
minnet_client_pollfd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  SyncFetch* c = ptr;
  int32_t fd;
  BOOL rd = JS_ToBool(ctx, argv[1]), wr = JS_ToBool(ctx, argv[2]);

  JS_ToInt32(ctx, &fd, argv[0]);

  if(!rd && !wr) {
    synchfetch_removefd(ptr, fd);
  } else {
    synchfetch_setevents(ptr, fd, (rd ? POLLIN : 0) | (wr ? POLLOUT : 0));
  }

#if 1 // def DEBUG_OUTPUT
  printf("%s argc=%d fd=%d rd=%d wr=%d c->nfds=%zu\n", __func__, argc, fd, rd, wr, c->nfds);
#endif

  return JS_UNDEFINED;
}

JSValue
minnet_client_onclose(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  SyncFetch* c = ptr;
  int32_t fd = -1;
  MinnetWebsocket* ws;
  BOOL is_error = argc > 1 && JS_IsError(ctx, argv[1]);

  if((ws = minnet_ws_data(argv[0])) && ws->fd != -1)
    fd = ws->fd;
  else
    JS_ToInt32(ctx, &fd, argv[0]);

  printf("%s err=%i argv[1]=%s\n", __func__, is_error, JS_ToCString(ctx, argv[1]));

  if(fd != -1)
    synchfetch_removefd(c, fd);

  if(is_error) {
    JS_Throw(ctx, argv[1]);
    c->exception = JS_DupValue(ctx, argv[1]);
  }

#ifdef DEBUG_OUTPUT
  printf("%s fd=%d c=%p c->nfds=%zu\n", __func__, fd, c, c->nfds);
#endif

  return JS_UNDEFINED;
}

JSValue
minnet_client_closure(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  int argind = 0;
  JSValue value, ret = JS_NULL;
  MinnetClient* client = 0;
  JSValue options = argv[0];
  struct lws* wsi2;
  BOOL block = FALSE;
  // struct wsi_opaque_user_data* opaque = 0;
  MinnetProtocol proto;

  // SETLOG(LLL_INFO)

  if(!(client = client_new(ctx)))
    return JS_EXCEPTION;

  client_zero(client);

  if(ptr) {
    union closure* closure = ptr;

    closure->pointer = client;
    closure->free_func = (closure_free_t*)client_free;
  }

  if(!(client->request = request_from(argc, argv, ctx))) {
    client_free(client, JS_GetRuntime(ctx));
    return JS_ThrowTypeError(ctx, "argument 1 must be a Request/URL object or an URL string");
  }

  js_async_zero(&client->promise);

  if(argc >= 2) {

    if(JS_IsObject(argv[1]))
      argind++;

    /*    if(JS_IsString(argv[argind])) {
          tmp = JS_ToCString(ctx, argv[argind]);
          argind++;
        } else if((req = minnet_request_data(argv[argind]))) {
          client->request = request_dup(req);
          client->url = url_clone(req->url, ctx);
          client->headers = JS_GetPropertyStr(ctx, argv[argind], "headers");
          argind++;
        }*/
  }

  options = argv[argind];

  if(!JS_IsObject(options))
    return JS_ThrowTypeError(ctx, "argument %d must be options object", argind + 1);

  {
    struct context* context = &client->context;

    context->js = ctx;
    context->error = JS_NULL;

    memset(&context->info, 0, sizeof(struct lws_context_creation_info));
    context->info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    context->info.options |= LWS_SERVER_OPTION_H2_JUST_FIX_WINDOW_UPDATE_OVERFLOW;
    context->info.port = CONTEXT_PORT_NO_LISTEN;
    context->info.protocols = client_protocols;
    context->info.user = client;

    if(!context->lws) {
      //  client_certificate(&client->context, options);

      if(!(context->lws = lws_create_context(&context->info))) {
        lwsl_err("minnet-client: libwebsockets init failed\n");
        return JS_ThrowInternalError(ctx, "minnet-client: libwebsockets init failed");
      }
    }
  }

  GETCBPROP(options, "onPong", client->on.pong)
  GETCBPROP(options, "onClose", client->on.close)
  GETCBPROP(options, "onConnect", client->on.connect)
  GETCBPROP(options, "onMessage", client->on.message)
  GETCBPROP(options, "onResponse", client->on.http)
  GETCBPROP(options, "onFd", client->on.fd)
  GETCBPROP(options, "onWriteable", client->on.writeable)

  if(!JS_IsFunction(ctx, client->on.fd.func_obj)) {
    client->on.fd = CALLBACK_INIT(ctx, minnet_default_fd_callback(ctx), JS_NULL);
  }

  value = JS_GetPropertyStr(ctx, options, "block");
  if(!JS_IsUndefined(value))
    block = client->block = JS_ToBool(ctx, value);
  JS_FreeValue(ctx, value);

  value = JS_GetPropertyStr(ctx, options, "body");
  if(!JS_IsUndefined(value))
    client->body = JS_DupValue(ctx, value);
  JS_FreeValue(ctx, value);

  /* value = JS_GetPropertyStr(ctx, options, "headers");
   if(JS_IsObject(value)) {
     client->headers = JS_DupValue(ctx, value);
   }
   JS_FreeValue(ctx, value);

   if(JS_IsObject(client->headers)) {
     headers_fromobj(&client->request->headers, client->headers, ctx);
   }
 */
  client->response = response_new(ctx);
  url_copy(&client->response->url, client->request->url, ctx);

  // client->response->body = generator_new(ctx);

  proto = protocol_number(client->request->url.protocol);

  url_info(client->request->url, &client->connect_info);

  value = JS_GetPropertyStr(ctx, options, "protocol");
  if(!JS_IsUndefined(value)) {
    const char* str = JS_ToCString(ctx, value);

    if(client->connect_info.protocol)
      free((void*)client->connect_info.protocol);

    client->connect_info.protocol = strdup(str);
    JS_FreeCString(ctx, str);
  }
  JS_FreeValue(ctx, value);

  DEBUG("alpn = '%s'\n", client->connect_info.alpn);

  client->connect_info.pwsi = &client->wsi;
  client->connect_info.context = client->context.lws;

  switch(proto) {
    case PROTOCOL_RAW: {
      client->connect_info.method = js_strdup(ctx, "RAW");
      break;
    }

    case PROTOCOL_HTTP:
    case PROTOCOL_HTTPS: {
      const char* str;

      value = JS_GetPropertyStr(ctx, options, "method");
      str = JS_ToCString(ctx, value);
      client->connect_info.method = js_strdup(ctx, JS_IsString(value) ? str : method_string(METHOD_GET));
      JS_FreeValue(ctx, value);
      JS_FreeCString(ctx, str);

      break;
    }
    default: {
      break;
    }
  }

  DEBUG("METHOD: %s\n", client->connect_info.method);
  DEBUG("PROTOCOL: %s\n", client->connect_info.protocol);

  if(!block) {
    ret = js_async_create(ctx, &client->promise);

    if(JS_IsNull(client->on.connect.func_obj))
      client->on.connect = CALLBACK_INIT(ctx, JS_DupValue(ctx, client->promise.resolve), JS_NULL);
    if(JS_IsNull(client->on.close.func_obj))
      client->on.close = CALLBACK_INIT(ctx, JS_DupValue(ctx, client->promise.reject), JS_NULL);
  }

  errno = 0;
  SyncFetch* c = 0;

  if(block) {

    if(!(c = malloc(sizeof(SyncFetch))))
      return JS_ThrowOutOfMemory(ctx);

    c->ref_count = 1;
    c->nfds = 0;
    c->pfds = 0;
    c->touched = FALSE;
    c->exception = JS_NULL;

    client->on.fd = CALLBACK_INIT(ctx, js_function_cclosure(ctx, minnet_client_pollfd, 0, 0, synchfetch_dup(c), synchfetch_free), JS_UNDEFINED);
    JS_DefinePropertyValueStr(ctx, client->on.fd.func_obj, "name", JS_NewString(ctx, "onFd"), 0);

    client->on.close = CALLBACK_INIT(ctx, js_function_cclosure(ctx, minnet_client_onclose, 0, 0, synchfetch_dup(c), synchfetch_free), JS_UNDEFINED);
  }

#ifdef LWS_WITH_UDP
  if(proto == PROTOCOL_RAW && !strcmp(client->request->url.protocol, "udp")) {
    struct lws_vhost* vhost;
    MinnetURL* url = &client->request->url;

    vhost = lws_create_vhost(client->context.lws, &client->context.info);
    wsi2 = lws_create_adopt_udp(vhost, url->host, url->port, 0, "raw", 0, 0, 0, 0, 0);
    *client->connect_info.pwsi = wsi2;
  } else
#endif
  {
    wsi2 = lws_client_connect_via_info(&client->connect_info);
  }
  DEBUG("client->wsi = %p, wsi2 = %p, h2 = %d, ssl = %d\n", client->wsi, wsi2, wsi_http2(client->wsi), wsi_tls(client->wsi));

  if(!client->wsi /*&& !wsi2*/) {
    if(!block) {
      if(js_async_pending(&client->promise)) {
        JSValue err = js_error_new(ctx, "[2] Connection failed: %s", strerror(errno));
        js_async_reject(ctx, &client->promise, err);
        JS_FreeValue(ctx, err);
      }
    } else {
      ret = JS_ThrowInternalError(ctx, "Connection failed: %s", strerror(errno));
      goto fail;
    }
  }

  /*opaque = lws_opaque(client->wsi, client->context.js);
  opaque->binary = binary;
  opaque->connect_info = &client->connect_info;
  opaque->req = client->request;
  opaque->resp = client->response;*/

  switch(magic) {
    case RETURN_CLIENT: {
      ret = minnet_client_wrap(ctx, client);
      break;
    }

    case RETURN_RESPONSE: {
      if(!block) {
        ResolveFunctions fns;
        ret = js_async_create(ctx, &fns);

        client->on.http = CALLBACK_INIT(ctx, JS_NewCFunctionData(ctx, minnet_client_response, 2, 0, 2, &fns.resolve), JS_UNDEFINED);
        js_async_free(JS_GetRuntime(ctx), &fns);
      } else {
        synchfetch_setevents(c, lws_get_socket_fd(lws_get_network_wsi(client->wsi)), POLLIN | POLLOUT);

        struct wsi_opaque_user_data* opaque = lws_opaque(client->wsi, ctx);
        opaque->resp = client->response;
        Generator* gen = response_generator(opaque->resp, ctx);

        assert(opaque->resp->body);
        generator_continuous(gen, JS_NULL);

        opaque->resp->sync = TRUE;

        for(;;) {

#ifdef DEBUG_OUTPUT
          printf("%s c->nfds=%zu\n", __func__, c->nfds);
#endif
          int r = poll(c->pfds, c->nfds, 1000);

          c->touched = FALSE;

          for(size_t i = 0; i < c->nfds; i++) {
            struct pollfd* x = &c->pfds[i];

#ifdef DEBUG_OUTPUT
            printf("%s c->pfds[%zu] = { %d, %d, %d }\n", __func__, i, x->fd, x->events, x->revents);
#endif

            if(x->events) {
              struct lws_pollfd lpfd = {x->fd, x->events, x->revents};
              int result = lws_service_fd(client->context.lws, &lpfd);
            }

            if(c->touched)
              break;
          }

          // printf("%s c->nfds=%zu\n", __func__, c->nfds);

          if(c->nfds == 0)
            break;
        }

        if(!JS_IsNull(c->exception))
          ret = JS_Throw(ctx, c->exception);
        else
          ret = JS_DupValue(ctx, client->session.resp_obj);

        synchfetch_free(c);
      }

      break;
    }
  }

/*  for(;;) {
    if(status != opaque->status)
      status = opaque->status;

    if(status == CLOSED)
      break;

    js_std_loop(ctx);

    if(client->context.exception) {
      ret = JS_EXCEPTION;
      break;
    }
  }

  // client_free(client);
*/
fail:
  return ret;
}
static void
minnet_client_finalizer(JSRuntime* rt, JSValue val) {
  MinnetClient* client;

  if((client = minnet_client_data(val))) {
    client_free(client, rt);
  }
}

static const JSClassDef minnet_client_class = {
    "MinnetClient",
    .finalizer = minnet_client_finalizer,
};

static const JSCFunctionListEntry minnet_client_proto_funcs[] = {
    JS_CGETSET_MAGIC_FLAGS_DEF("request", minnet_client_get, 0, CLIENT_REQUEST, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("response", minnet_client_get, 0, CLIENT_RESPONSE, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("socket", minnet_client_get, 0, CLIENT_SOCKET, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("onmessage", minnet_client_get, minnet_client_set, CLIENT_ONMESSAGE, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("onconnect", minnet_client_get, minnet_client_set, CLIENT_ONCONNECT, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("onclose", minnet_client_get, minnet_client_set, CLIENT_ONCLOSE, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("onpong", minnet_client_get, minnet_client_set, CLIENT_ONPONG, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("onfd", minnet_client_get, minnet_client_set, CLIENT_ONFD, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("onhttp", minnet_client_get, minnet_client_set, CLIENT_ONHTTP, 0),
    JS_CGETSET_MAGIC_FLAGS_DEF("onwriteable", minnet_client_get, minnet_client_set, CLIENT_ONWRITEABLE, 0),
    // JS_CFUNC_MAGIC_DEF("[Symbol.asyncIterator]", 0, minnet_client_iterator, CLIENT_ASYNCITERATOR),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetClient", JS_PROP_CONFIGURABLE),
};

JSValue
minnet_client(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  union closure* closure;
  JSValue ret;
  MinnetClient* cl;

  if(!(closure = closure_new(ctx)))
    return JS_EXCEPTION;

  ret = minnet_client_closure(ctx, this_val, argc, argv, 0, closure);

  /*
   if(js_is_promise(ctx, ret)) {
    JSValue func[2], tmp;

    func[0] = js_function_cclosure(ctx, &minnet_client_handler, 1, ON_RESOLVE, closure_dup(closure), closure_free);
    func[1] = js_function_cclosure(ctx, &minnet_client_handler, 1, ON_REJECT, closure_dup(closure), closure_free);

    tmp = js_invoke(ctx, ret, "then", 1, &func[0]);
    JS_FreeValue(ctx, ret);
    ret = tmp;

    tmp = js_invoke(ctx, ret, "catch", 1, &func[1]);
    JS_FreeValue(ctx, ret);
    ret = tmp;

    JS_FreeValue(ctx, func[0]);
    JS_FreeValue(ctx, func[1]);

    closure_free(closure);
  }*/
  ret = minnet_client_wrap(ctx, cl);
  if((cl = closure->pointer)) {
    JSValue fn = JS_NewCFunctionMagic(ctx, minnet_client_iterator, "iterator", 0, JS_CFUNC_generic_magic, cl->block ? CLIENT_ITERATOR : CLIENT_ASYNCITERATOR);
    JSAtom prop = js_symbol_static_atom(ctx, cl->block ? "iterator" : "asyncIterator");

    JS_SetProperty(ctx, ret, prop, fn);
    JS_FreeAtom(ctx, prop);
  }
  return ret;
}

JSValue
minnet_client_wrap(JSContext* ctx, MinnetClient* client) {
  JSValue ret = JS_NewObjectProtoClass(ctx, minnet_client_proto, minnet_client_class_id);

  if(JS_IsException(ret))
    return JS_EXCEPTION;

  JS_SetOpaque(ret, (client));

  /*JS_SetPropertyFunctionList(ctx, ret, client->block ? minnet_client_sync_funcs : minnet_client_async_funcs, 1);*/

  return ret;
}

int
minnet_client_init(JSContext* ctx, JSModuleDef* m) {
  // Add class Client
  JS_NewClassID(&minnet_client_class_id);

  JS_NewClass(JS_GetRuntime(ctx), minnet_client_class_id, &minnet_client_class);
  minnet_client_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, minnet_client_proto, minnet_client_proto_funcs, countof(minnet_client_proto_funcs));
  JS_SetClassProto(ctx, minnet_client_class_id, minnet_client_proto);

  minnet_client_ctor = JS_NewObject(ctx);
  JS_SetConstructor(ctx, minnet_client_ctor, minnet_client_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Client", minnet_client_ctor);

  return 0;
}

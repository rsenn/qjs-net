#include "callback.h"
#include "jsutils.h"
#include "utils.h"
#include "opaque.h"
#include <assert.h>

#define PIO (POLLIN | POLLOUT | POLLERR)

typedef struct handler_closure {
  JSContext* ctx;
  struct lws* lwsi;
  struct wsi_opaque_user_data* opaque;
} MinnetHandler;

static void
free_handler_closure(void* ptr) {
  MinnetHandler* closure = ptr;
  JSContext* ctx = closure->ctx;
  js_free(ctx, closure);
};

int
fd_handler(struct lws* wsi, MinnetCallback* cb, struct lws_pollargs args) {
  JSValue argv[3] = {JS_NewInt32(cb->ctx, args.fd)};

  callback_handlers(cb->ctx, wsi, args, &argv[1]);
  callback_emit(cb, 3, argv);

  JS_FreeValue(cb->ctx, argv[0]);
  JS_FreeValue(cb->ctx, argv[1]);
  JS_FreeValue(cb->ctx, argv[2]);
  return 0;
}

int
fd_callback(struct lws* wsi, enum lws_callback_reasons reason, MinnetCallback* cb, struct lws_pollargs* args) {

  switch(reason) {
    case LWS_CALLBACK_LOCK_POLL:
    case LWS_CALLBACK_UNLOCK_POLL: return 0;

    case LWS_CALLBACK_ADD_POLL_FD: {

      if(cb->ctx) {
        fd_handler(wsi, cb, *args);
      }
      return 0;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {

      if(cb->ctx) {
        fd_handler(wsi, cb, *args);
      }
      return 0;
    }
    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
      if(cb->ctx) {
        if(args->events != args->prev_events) {
          fd_handler(wsi, cb, *args);
        }
      }
      return 0;
    }

    default: {
      return -1;
    }
  }
}

JSValue
callback_emit_this(const struct ws_callback* cb, JSValueConst this_obj, int argc, JSValue* argv) {
  JSValue ret = JS_UNDEFINED;

  if(cb->ctx) {

    ret = JS_Call(cb->ctx, cb->func_obj, this_obj, argc, argv);
  }

  if(JS_IsException(ret)) {
    JSValue exception = JS_GetException(cb->ctx);
    js_error_print(cb->ctx, exception);
    ret = JS_Throw(cb->ctx, exception);
  }
  /*if(JS_IsException(ret))
    minnet_exception = TRUE; */

  return ret;
}

JSValue
callback_emit(const struct ws_callback* cb, int argc, JSValue* argv) {
  return callback_emit_this(cb, cb->this_obj /* ? *cb->this_obj : JS_NULL*/, argc, argv);
}

static JSValue
minnet_io_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* ptr) {
  MinnetHandler* closure = ptr;
  struct pollfd* p;
  int32_t wr;
  JSValue ret = JS_UNDEFINED;

  assert(closure->opaque);
  p = &closure->opaque->poll;

  JS_ToInt32(ctx, &wr, argv[0]);

  p->revents = magic & (wr == WRITE_HANDLER ? POLLOUT : POLLIN);

  if((p->revents & PIO) != magic) {
    if(poll(p, 1, 0) < 0)
      lwsl_err("poll error: %s\n", strerror(errno));
  }

  if(p->revents & PIO) {
    struct lws_pollfd x = {p->fd, magic, p->revents & PIO};

    if(p->revents & (POLLERR | POLLHUP))
      closure->opaque->error = errno;

    /*if(x.revents & POLLOUT)
      if(x.revents & POLLIN)
        x.revents &= ~(POLLOUT);*/
    // errno = 0;

    ret = JS_NewInt32(ctx, lws_service_fd(lws_get_context(closure->lwsi), &x));
  }

  return ret;
}

static JSValue
make_handler(JSContext* ctx, int fd, int events, struct lws* wsi) {
  MinnetHandler* closure;

  if(!(closure = js_mallocz(ctx, sizeof(MinnetHandler))))
    return JS_ThrowOutOfMemory(ctx);

  *closure = (MinnetHandler){ctx, wsi, lws_opaque(wsi, ctx)};

  closure->opaque->poll = (struct pollfd){fd, events, 0};

  return JS_NewCClosure(ctx, minnet_io_handler, 1, events, closure, free_handler_closure);
}

void
callback_handlers(JSContext* ctx, struct lws* wsi, struct lws_pollargs args, JSValue out[2]) {
  JSValue func;
  int events = args.events & (POLLIN | POLLOUT);
  // struct wsi_opaque_user_data*opaque =lws_opaque(wsi, ctx);

  if(events)
    func = make_handler(ctx, args.fd, events, wsi);

  out[0] = (events & POLLIN) ? js_function_bind_1(ctx, func, JS_NewInt32(ctx, READ_HANDLER)) : JS_NULL;
  out[1] = (events & POLLOUT) ? js_function_bind_1(ctx, func, JS_NewInt32(ctx, WRITE_HANDLER)) : JS_NULL;

  if(events)
    JS_FreeValue(ctx, func);
}

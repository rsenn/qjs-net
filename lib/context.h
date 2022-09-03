#ifndef MINNET_CONTEXT_H
#define MINNET_CONTEXT_H

#include <quickjs.h>
#include <cutils.h>
#include <libwebsockets.h>

typedef struct context {
  int ref_count;
  JSContext* js;
  struct lws_context* lws;
  struct lws_context_creation_info info;
  BOOL exception;
  JSValue error;
  JSValue crt, key, ca;
  struct TimerClosure* timer;
} MinnetContext;

JSValue context_exception(MinnetContext*, JSValueConst retval);
void context_clear(MinnetContext*);

#endif /* MINNET_CONTEXT_H */

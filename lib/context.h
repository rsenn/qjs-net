#ifndef QJSNET_LIB_CONTEXT_H
#define QJSNET_LIB_CONTEXT_H

#include <quickjs.h>
#include <cutils.h>
#include <list.h>
#include <libwebsockets.h>
#include "jsutils.h"

struct context {
  int ref_count;
  JSContext* js;
  struct lws_context* lws;
  ResolveFunctions promise;
  BOOL exception;
  JSValue error;
  JSValue crt, key, ca;
  struct TimerClosure* timer;
  struct list_head link;
  struct lws_context_creation_info info;
};

JSValue context_exception(struct context*, JSValueConst retval);
void context_clear(struct context*);
void context_add(struct context*);
void context_delete(struct context*);
struct context* context_for_fd(int, struct lws** p_wsi);

#endif /* QJSNET_LIB_CONTEXT_H */

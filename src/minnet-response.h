#ifndef MINNET_RESPONSE_H
#define MINNET_RESPONSE_H

#include "session.h"
#include "response.h"
#include "minnet-url.h"

typedef struct http_response MinnetResponse;

MinnetResponse* minnet_response_data(JSValueConst);
JSValue minnet_response_new(JSContext*, MinnetURL, int, const char*, BOOL, const char*);
JSValue minnet_response_wrap(JSContext*, MinnetResponse*);
JSValue minnet_response_constructor(JSContext*, JSValueConst, int, JSValueConst[]);
void minnet_response_finalizer(JSRuntime*, JSValueConst);
int minnet_response_init(JSContext*, JSModuleDef*);

extern THREAD_LOCAL JSClassID minnet_response_class_id;
extern THREAD_LOCAL JSValue minnet_response_proto, minnet_response_ctor;

static inline MinnetResponse*
minnet_response_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_response_class_id);
}

static inline Queue*
session_queue(struct session_data* session) {
  Response* resp;

  if(queue_size(&session->sendq))
    return &session->sendq;

  if((resp = minnet_response_data(session->resp_obj)))
    if(resp->body)
      return resp->body->q;

  return NULL;
}

#endif /* MINNET_RESPONSE_H */

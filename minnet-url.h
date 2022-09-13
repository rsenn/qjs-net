#ifndef MINNET_URL_H
#define MINNET_URL_H

#include <libwebsockets.h>
#include <quickjs.h>
#include <cutils.h>
#include <stdint.h>
#include "utils.h"
#include "url.h"

typedef enum protocol MinnetProtocol;

typedef struct url MinnetURL;

JSValue minnet_url_wrap(JSContext*, MinnetURL*);
JSValue minnet_url_new(JSContext*, MinnetURL);
JSValue minnet_url_method(JSContext*, JSValueConst, int, JSValueConst argv[], int magic);
JSValue minnet_url_from(JSContext*, JSValueConst, int, JSValueConst argv[]);
JSValue minnet_url_inspect(JSContext*, JSValueConst, int, JSValueConst argv[]);
JSValue minnet_url_constructor(JSContext*, JSValueConst, int, JSValueConst argv[]);
int minnet_url_init(JSContext*, JSModuleDef*);

extern THREAD_LOCAL JSClassID minnet_url_class_id;

int minnet_url_init(JSContext*, JSModuleDef* m);

static inline MinnetURL*
minnet_url_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_url_class_id);
}

static inline MinnetURL*
minnet_url_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_url_class_id);
}

#endif /* MINNET_URL_H */

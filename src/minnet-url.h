#ifndef MINNET_URL_H
#define MINNET_URL_H

#include "url.h"

typedef enum protocol MinnetProtocol;

typedef struct url MinnetURL;

MinnetURL* minnet_url_data(JSValueConst);
JSValue minnet_url_wrap(JSContext*, MinnetURL* url);
JSValue minnet_url_new(JSContext*, MinnetURL u);
JSValue minnet_url_method(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[], int magic);
JSValue minnet_url_from(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[]);
JSValue minnet_url_inspect(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[]);
JSValue minnet_url_constructor(JSContext*, JSValueConst new_target, int argc, JSValueConst argv[]);
int minnet_url_init(JSContext*, JSModuleDef* m);

extern THREAD_LOCAL JSClassID minnet_url_class_id;
extern THREAD_LOCAL JSValue minnet_url_proto, minnet_url_ctor;

int minnet_url_init(JSContext*, JSModuleDef* m);

static inline MinnetURL*
minnet_url_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_url_class_id);
}

#endif /* MINNET_URL_H */

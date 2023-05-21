#ifndef MINNET_HASH_H
#define MINNET_HASH_H

#include "utils.h"
#include "js-utils.h"
#include <libwebsockets.h>

typedef struct hash_hmac {
  union {
    struct lws_genhash_ctx hash;
    struct lws_genhmac_ctx hmac;
  } lws;
  uint8_t type;
  BOOL hmac : 1, initialized : 1, finalized : 1;
  uint8_t digest[0];
} MinnetHash;

JSValue minnet_hash_constructor(JSContext*, JSValueConst, int, JSValueConst[]);
int minnet_hash_init(JSContext*, JSModuleDef*);

extern THREAD_LOCAL JSValue minnet_hash_proto, minnet_hash_ctor;
extern THREAD_LOCAL JSClassID minnet_hash_class_id;

static inline MinnetHash*
minnet_hash_data(JSValueConst obj) {
  return JS_GetOpaque(obj, minnet_hash_class_id);
}

static inline MinnetHash*
minnet_hash_data2(JSContext* ctx, JSValueConst obj) {
  return JS_GetOpaque2(ctx, obj, minnet_hash_class_id);
}

#endif /* MINNET_HASH_H */

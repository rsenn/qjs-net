#ifndef MINNET_GENERATOR_H
#define MINNET_GENERATOR_H

#include "minnet.h"
#include "jsutils.h"
#include <libwebsockets.h>
#include <pthread.h>
#include "minnet-buffer.h"

typedef struct generator {
  JSContext* ctx;
  int ref_count;
  AsyncIterator iterator;
  MinnetBuffer buffer;
} MinnetGenerator;

void generator_zero(struct generator*);
void generator_free(struct generator*);
struct generator* generator_new(JSContext*);
struct generator* generator_dup(struct generator*);
JSValue generator_next(MinnetGenerator*, JSContext*);
JSValue minnet_generator_wrap(JSContext*, MinnetGenerator*);

#endif /* MINNET_GENERATOR_H */

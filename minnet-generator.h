#ifndef MINNET_GENERATOR_H
#define MINNET_GENERATOR_H

#include "minnet.h"
#include "jsutils.h"
#include <libwebsockets.h>
#include <pthread.h>
#include "minnet-buffer.h"

typedef struct generator {
  MinnetBuffer buffer;
  AsyncIterator iterator;
  JSContext* ctx;
  int ref_count;
} MinnetGenerator;

void generator_zero(struct generator*);
void generator_free(struct generator**);
struct generator* generator_new(JSContext*);
JSValue generator_next(MinnetGenerator*, JSContext*);
ssize_t generator_write(MinnetGenerator*, const void*, size_t);
JSValue minnet_generator_wrap(JSContext*, MinnetGenerator**);

#endif /* MINNET_GENERATOR_H */

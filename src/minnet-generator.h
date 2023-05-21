#ifndef MINNET_GENERATOR_H
#define MINNET_GENERATOR_H

#include "generator.h"

typedef struct generator MinnetGenerator;

JSValue minnet_generator_constructor(JSContext*, JSValueConst, int, JSValueConst[]);
JSValue minnet_generator_iterator(JSContext*, MinnetGenerator*);
JSValue minnet_generator_create(JSContext*, MinnetGenerator**);
int minnet_generator_init(JSContext*, JSModuleDef*);

extern THREAD_LOCAL JSValue minnet_generator_proto, minnet_generator_ctor;
extern THREAD_LOCAL JSClassID minnet_generator_class_id;

#endif /* MINNET_GENERATOR_H */

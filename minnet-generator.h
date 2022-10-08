#ifndef MINNET_GENERATOR_H
#define MINNET_GENERATOR_H

#include "generator.h"

typedef struct generator MinnetGenerator;
extern THREAD_LOCAL JSValue minnet_generator_proto, minnet_generator_ctor;

JSValue minnet_generator_constructor(JSContext*, JSValueConst new_target, int argc, JSValueConst argv[]);
JSValue minnet_generator_wrap(JSContext*, MinnetGenerator* gen);
JSValue minnet_generator_create(JSContext*, MinnetGenerator** gen_p);

#endif /* MINNET_GENERATOR_H */

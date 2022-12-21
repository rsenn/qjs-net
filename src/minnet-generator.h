#ifndef MINNET_GENERATOR_H
#define MINNET_GENERATOR_H

#include "generator.h"

typedef struct generator MinnetGenerator;

extern THREAD_LOCAL JSValue minnet_generator_proto, minnet_generator_ctor;
extern THREAD_LOCAL JSClassID minnet_generator_class_id;
extern JSClassDef minnet_generator_class;
extern const JSCFunctionListEntry minnet_generator_proto_funcs[];
extern const size_t minnet_generator_proto_funcs_size;

JSValue minnet_generator_constructor(JSContext*, JSValueConst new_target, int argc, JSValueConst argv[]);
JSValue minnet_generator_iterator(JSContext*, MinnetGenerator* gen);
JSValue minnet_generator_reader(JSContext*, MinnetGenerator* gen);
JSValue minnet_generator_create(JSContext*, MinnetGenerator** gen_p);

#endif /* MINNET_GENERATOR_H */

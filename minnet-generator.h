#ifndef MINNET_GENERATOR_H
#define MINNET_GENERATOR_H

#include "generator.h"

typedef struct generator MinnetGenerator;

JSValue minnet_generator_create(JSContext*, MinnetGenerator**);

#endif /* MINNET_GENERATOR_H */

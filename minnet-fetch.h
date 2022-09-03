#ifndef MINNET_FETCH_H
#define MINNET_FETCH_H

#include <quickjs.h>

struct fetch_closure;

typedef struct fetch_closure MinnetFetch;

MinnetFetch* fetch_new(JSContext*);
MinnetFetch* fetch_dup(MinnetFetch*);
void fetch_free(void*);
JSValue minnet_fetch(JSContext*, JSValueConst this_val, int argc, JSValueConst argv[]);

#endif /* MINNET_FETCH_H */

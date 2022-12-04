#ifndef QUICKJS_NET_LIB_DEFERRED_H
#define QUICKJS_NET_LIB_DEFERRED_H

typedef struct deferred {
  void* (*func)(void*, void*, void*, void*, void*, void*, void*, void*);
  void* args[8];
} Deferred;

void deferred_clear(Deferred*);
void deferred_init(Deferred*, void* fn, int argc, void* argv[]);
void* deferred_call(const Deferred*);

#endif /* QUICKJS_NET_LIB_DEFERRED_H */

#ifndef QJSNET_LIB_ALLOCATED_H
#define QJSNET_LIB_ALLOCATED_H

#include <quickjs.h>

typedef struct allocated {
  void* pointer;
  void (*free_func)();
} Allocated;

static inline void
allocated_init(Allocated* alloc, void* ptr, void (*free_func)()) {
  alloc->pointer = ptr;
  alloc->free_func = free_func;
}

#endif /* QJSNET_LIB_ALLOCATED_H */

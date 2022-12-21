#ifndef QJSNET_LIB_ALLOCATED_H
#define QJSNET_LIB_ALLOCATED_H

#include <stddef.h>

typedef struct allocated {
<<<<<<< HEAD
  void* pointer;
  void (*free_func)(void*);
=======
  void *pointer, *opaque;
  void (*free_func)(void* ptr, void* opaque);
>>>>>>> 2e676673f404167abe81e74e17b79ec9303560af
} Allocated;

#define ALLOCATED(ptr, free_func) \
  (Allocated) { (ptr), (free_func) }

static inline void
allocated_init(Allocated* alloc, void* ptr, void (*free_func)(void* ptr, void* opaque)) {
  alloc->pointer = ptr;
  alloc->free_func = free_func;
}

static inline void
allocated_free(Allocated* alloc) {
  if(alloc->pointer) {
    alloc->free_func(alloc->pointer, alloc->opaque);
    alloc->pointer = NULL;
  }
}

static inline void
allocated_zero(Allocated* alloc) {
  alloc->pointer = NULL;
  alloc->free_func = NULL;
}

#endif /* QJSNET_LIB_ALLOCATED_H */

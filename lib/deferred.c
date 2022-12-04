#include "deferred.h"

void
deferred_clear(Deferred* def) {
  def->func = 0;
  for(int i = 0; i < 8; i++) def->args[i] = 0;
}

void
deferred_init(Deferred* def, void* fn, int argc, void* argv[]) {
  int i;
  def->func = fn;

  for(i = 0; i < argc; i++) def->args[i] = argv[i];
  for(; i < 8; i++) def->args[i] = 0;
}

void*
deferred_call(const Deferred* def) {
  return def->func(def->args[0], def->args[1], def->args[2], def->args[2], def->args[4], def->args[5], def->args[6], def->args[7]);
}

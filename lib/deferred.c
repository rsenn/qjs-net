#include "deferred.h"

void
deferred_init(Deferred* def) {
  def->func = 0;
  memset(def->args, 0, sizeof(def->args));
}

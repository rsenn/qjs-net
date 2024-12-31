/**
 * @file deferred.c
 */
#include <assert.h>
#include "deferred.h"
#include "js-utils.h"

void
deferred_clear(Deferred* def) {
  def->num_calls = 0;
  def->only_once = FALSE;
  def->func = 0;

  for(int i = 0; i < 8; i++)
    def->argv[i] = 0;
}

void
deferred_free(Deferred* def) {
  if(def->next)
    deferred_destructor(def->next);

  if(--def->ref_count == 0) {
    deferred_clear(def);
    free(def);
  }
}

Deferred*
deferred_newv(ptr_t fn, int argc, ptr_t argv[]) {
  Deferred* def;

  if(!(def = malloc(sizeof(Deferred))))
    return 0;

  deferred_init(def, fn, argc, argv);

  def->ref_count = 1;
  def->num_calls = 0;
  def->only_once = FALSE;

  return def;
}

Deferred*
deferred_newjs(JSValue func, JSContext* ctx) {
  Deferred* def;
  /*ptr_t args[] = {
      ctx,
      ((ptr_t*)&v)[0],
      ((ptr_t*)&v)[1],
  };
  def = deferred_newv(fn, 3, args);*/
  def = deferred_new(&JS_Call, ctx, func, JS_UNDEFINED);

  def->next = deferred_new(&JS_FreeValue, ctx, func);
  return def;
}

void
deferred_init(Deferred* def, ptr_t fn, int argc, ptr_t argv[]) {
  int i;

  def->ref_count = 0;
  def->func = fn;

  for(i = 0; i < 8; i++)
    def->argv[i] = i < argc ? argv[i] : 0;

  def->argc = argc;
  def->num_calls = 0;
  def->only_once = FALSE;
  def->next = 0;
}

DoubleWord
deferred_call_x(Deferred* def, ...) {
  ptr_t const* av = def->argv;
  DoubleWord ret = {{0, 0}};
  va_list a;
  int argc = def->argc;
  size_t arg;

  va_start(a, def);

  while(argc < (int)countof(def->argv) && (arg = va_arg(a, size_t))) {
    if(arg == DEFERRED_SENTINEL)
      break;
    def->argv[argc++] = (ptr_t)arg;
  }

  va_end(a);

  assert(!def->only_once || def->num_calls < 1);

  if(!def->only_once || def->num_calls < 1) {

    if(def->func == (void*)&JS_Call)
      ret = def->func(av[0], av[1], av[2], av[3], av[4], (ptr_t)(size_t)((argc - def->argc) > 0 ? 1 : 0), (ptr_t)&av[def->argc], 0);
    else
      ret = def->func(av[0], av[1], av[2], av[3], av[4], av[5], av[6], av[7]);

    ++def->num_calls;
  }

  return ret;
}

void
deferred_destructor(void* ptr) {
  Deferred* def = ptr;

  do {
    ptr = def->next;
    def->next = NULL;
    deferred_call(def);
    deferred_free(def);
  } while((def = ptr));
}

void
deferred_finalizer(JSRuntime* rt, void* opaque, void* ptr) {
  deferred_destructor(opaque);
}

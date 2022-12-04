#include <assert.h>
#include "deferred.h"

void
deferred_clear(Deferred* def) {
  def->num_calls = 0;
  def->only_once = FALSE;
  def->func = 0;
  def->retval = 0;

  for(int i = 0; i < 8; i++) { def->args[i] = 0; }
  def->opaque = 0;
}

void
deferred_free(Deferred* def) {
  if(--def->ref_count == 0) {
    JSContext* ctx = def->opaque;
    def->finalize(def);
    deferred_clear(def);
    js_free(ctx, def);
  }
}

Deferred*
deferred_new(ptr_t fn, int argc, ptr_t argv[], JSContext* ctx) {
  Deferred* def;

  if(!(def = js_malloc(ctx, sizeof(Deferred))))
    return 0;

  def->ref_count = 1;
  def->num_calls = 0;
  def->only_once = FALSE;
  def->opaque = ctx;

  deferred_init(def, fn, argc, argv);

  return def;
}

static void
deferred_freejs(Deferred* def) {
  JSValue value = deferred_getjs(def);

  JS_FreeValue(def->args[0], value);
}


Deferred*
deferred_newjs(ptr_t fn, JSValue  v, JSContext* ctx) {
  Deferred* ret;
   ptr_t args[] = {
      ctx,
      ((ptr_t*)&v)[0],
      ((ptr_t*)&v)[1],
  };

  ret = deferred_new(fn, 3, args, ctx);
  ret->finalize = deferred_freejs;
  return ret;
}

Deferred*
deferred_dupjs(ptr_t fn, JSValueConst value, JSContext* ctx) {
  JSValue v = JS_DupValue(ctx, value);
  return deferred_newjs(fn,v,ctx);
}


Deferred*
deferred_newjs_rt(ptr_t fn, JSValue value, JSContext* ctx) {
  ptr_t args[] = {
      JS_GetRuntime(ctx),
      ((ptr_t*)&value)[0],
      ((ptr_t*)&value)[1],
  };

  return deferred_new(fn, 3, args, ctx);
}

void
deferred_init(Deferred* def, ptr_t fn, int argc, ptr_t argv[]) {
  int i;

  def->ref_count = 0;
  def->func = fn;

  for(i = 0; i < 8; i++) { def->args[i] = i < argc ? argv[i] : 0; }

  def->num_calls = 0;
  def->only_once = FALSE;
}

ptr_t
deferred_call(Deferred* def) {
  ptr_t const* av = def->args;

  assert(!def->only_once || def->num_calls < 1);

  if(!def->only_once || def->num_calls < 1) {
    def->retval = def->func(av[0], av[1], av[2], av[2], av[4], av[5], av[6], av[7]);

    ++def->num_calls;
  }

  return def->retval;
}

static JSValue
deferred_js_call(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, ptr_t ptr) {
  Deferred* def = ptr;

  deferred_call(def);

  return JS_UNDEFINED;
}

JSValue
deferred_js(Deferred* def, JSContext* ctx) {
  deferred_dup(def);

  return JS_NewCClosure(ctx, deferred_js_call, 0, 0, def, (void (*)(ptr_t))deferred_free);
}

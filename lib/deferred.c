#include <assert.h>
#include "deferred.h"

void
deferred_clear(Deferred* def) {
  def->num_calls = 0;
  def->only_once = FALSE;
  def->func = 0;
  def->retval.lo = 0;
  def->retval.hi = 0;

  for(int i = 0; i < 8; i++) { def->args[i] = 0; }
  def->opaque = 0;
  // def->finalize = 0;
}

void
deferred_free(Deferred* def) {
  if(--def->ref_count == 0) {
    JSContext* ctx = def->opaque;
    // if(def->finalize) def->finalize(def);
    deferred_clear(def);
    js_free(ctx, def);
  }
}

Deferred*
deferred_new(ptr_t fn, int argc, ptr_t argv[], JSContext* ctx) {
  Deferred* def;

  if(!(def = js_malloc(ctx, sizeof(Deferred))))
    return 0;

  deferred_init(def, fn, argc, argv);

  def->ref_count = 1;
  def->num_calls = 0;
  def->only_once = FALSE;
  def->opaque = ctx;

  return def;
}

static void
deferred_freejs(Deferred* def) {
  JSValue value = deferred_getjs(def);

  JS_FreeValue(def->args[0], value);
}

static void
deferred_freejs_rt(Deferred* def) {
  JSValue value = deferred_getjs(def);

  JS_FreeValueRT(def->args[0], value);
}

Deferred*
deferred_newjs(js_ctx_function_t fn, JSValue v, JSContext* ctx) {
  Deferred* def;
  ptr_t args[] = {
      ctx,
      ((ptr_t*)&v)[0],
      ((ptr_t*)&v)[1],
  };

  def = deferred_new(fn, 3, args, ctx);
  // def->finalize = deferred_freejs;

  def->next = deferred_new1(deferred_freejs, ctx, ctx);
  return def;
}

Deferred*
deferred_dupjs(js_ctx_function_t fn, JSValueConst value, JSContext* ctx) {
  JSValue v = JS_DupValue(ctx, value);
  return deferred_newjs(fn, v, ctx);
}

Deferred*
deferred_newjs_rt(js_rt_function_t fn, JSValue value, JSContext* ctx) {
  Deferred* def;
  JSRuntime* rt = JS_GetRuntime(ctx);
  ptr_t args[] = {
      rt,
      ((ptr_t*)&value)[0],
      ((ptr_t*)&value)[1],
  };

  def = deferred_new(fn, 3, args, ctx);
  // def->finalize = deferred_freejs_rt;
  def->next = deferred_new1(deferred_freejs_rt, rt, ctx);
  return def;
}

void
deferred_init(Deferred* def, ptr_t fn, int argc, ptr_t argv[]) {
  int i;

  def->ref_count = 0;
  def->func = fn;

  for(i = 0; i < 8; i++) { def->args[i] = i < argc ? argv[i] : 0; }

  def->num_calls = 0;
  def->only_once = FALSE;
  def->retval = (DoubleWord){{0, 0}};
  def->opaque = 0;
  // def->finalize = 0;
}

DoubleWord
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

  return def->retval.js;
}

JSValue
deferred_js(Deferred* def, JSContext* ctx) {
  deferred_dup(def);

  return JS_NewCClosure(ctx, deferred_js_call, 0, 0, def, (void (*)(ptr_t))deferred_free);
}
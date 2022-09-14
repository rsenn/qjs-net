#include "asynciterator.h"

static AsyncRead* asynciterator_shift(AsyncIterator*, JSContext*);

void
asynciterator_zero(AsyncIterator* it) {
  it->ctx = 0;
  it->closed = FALSE;
  it->closing = FALSE;
  init_list_head(&it->reads);
  //  init_list_head(&it->values);
}

void
asynciterator_clear(AsyncIterator* it, JSRuntime* rt) {
  struct list_head *el, *next;

  list_for_each_safe(el, next, &it->reads) {
    AsyncRead* rd = list_entry(el, AsyncRead, link);
    list_del(&rd->link);
    js_promise_free_rt(rt, &rd->promise);
    js_free_rt(rt, rd);
  }

  // js_free_rt(rt, it);
}

AsyncIterator*
asynciterator_new(JSContext* ctx) {
  AsyncIterator* it;

  if((it = js_malloc(ctx, sizeof(AsyncIterator)))) {
    asynciterator_zero(it);
    it->ctx = ctx;
  }
  return it;
}

JSValue
asynciterator_next(AsyncIterator* it, JSContext* ctx) {
  AsyncRead* rd;
  JSValue ret = JS_UNDEFINED;

  if(it->closed)
    return JS_ThrowInternalError(ctx, "%s: iterator closed", __func__);

  if((rd = js_malloc(ctx, sizeof(AsyncRead)))) {
    list_add(&rd->link, &it->reads);
    ret = js_promise_create(ctx, &rd->promise);
  }

  asynciterator_check_closing(it, ctx);

  return ret;
}

BOOL
asynciterator_check_closing(AsyncIterator* it, JSContext* ctx) {

  if(it->closing) {
    asynciterator_stop(it, JS_UNDEFINED, ctx);
    it->closing = FALSE;
    it->closed = TRUE;
    return TRUE;
  }

  return FALSE;
}

static AsyncRead*
asynciterator_shift(AsyncIterator* it, JSContext* ctx) {
  if(!list_empty(&it->reads)) {
    AsyncRead* rd = (AsyncRead*)it->reads.prev;
    list_del(&rd->link);
    return rd;
  }
  return 0;
}

BOOL
asynciterator_yield(AsyncIterator* it, JSValueConst value, JSContext* ctx) {
  if(!list_empty(&it->reads)) {
    JSValue obj = asynciterator_obj(value, FALSE, ctx);

    return asynciterator_emplace(it, obj, ctx);
  }
  return FALSE;
}

int
asynciterator_reject_all(AsyncIterator* it, JSValueConst value, JSContext* ctx) {
  int ret = 0;
  AsyncRead* rd;

  while((rd = asynciterator_shift(it, ctx))) {
    js_promise_reject(ctx, &rd->promise, value);
    list_del(&rd->link);
    js_free(ctx, rd);
    ret++;
  }

  return ret;
}

BOOL
asynciterator_stop(AsyncIterator* it, JSValueConst value, JSContext* ctx) {
  BOOL ret = FALSE;

  if(!list_empty(&it->reads)) {
    JSValue obj = asynciterator_obj(value, TRUE, ctx);
    asynciterator_emplace(it, obj, ctx);
    it->closed = TRUE;

    asynciterator_reject_all(it, JS_NULL, ctx);
  } else {
    it->closing = TRUE;
  }
  if(it->closed)
    ret = TRUE;
  return ret;
}

BOOL
asynciterator_emplace(AsyncIterator* it, JSValueConst obj, JSContext* ctx) {
  AsyncRead* rd;
  if((rd = asynciterator_shift(it, ctx))) {
    js_promise_resolve(ctx, &rd->promise, obj);
    js_free(ctx, rd);
    return TRUE;
  }
  return FALSE;
}

JSValue
asynciterator_obj(JSValueConst value, BOOL done, JSContext* ctx) {
  JSValue obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, obj, "value", JS_DupValue(ctx, value));
  JS_SetPropertyStr(ctx, obj, "done", JS_NewBool(ctx, done));

  return obj;
}

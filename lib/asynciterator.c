#include "asynciterator.h"
#include "utils.h"

size_t
asynciterator_num_reads(AsyncIterator* it) {
  return list_size(&it->reads);
}

static AsyncRead*
asynciterator_shift(AsyncIterator* it, JSContext* ctx) {
  if(!list_empty(&it->reads)) {
    AsyncRead* rd = list_entry(it->reads.prev, AsyncRead, link);
    list_del(&rd->link);
    return rd;
  }
  return 0;
}

static BOOL
asynciterator_check_closing(AsyncIterator* it, JSContext* ctx) {
  if(it->closing) {
    asynciterator_stop(it, ctx);
    it->closing = FALSE;
    it->closed = TRUE;
    return TRUE;
  }

  return FALSE;
}

void
asynciterator_zero(AsyncIterator* it) {
  it->ref_count = 1;
  it->closed = FALSE;
  it->closing = FALSE;
  init_list_head(&it->reads);
}

void
asynciterator_clear(AsyncIterator* it, JSRuntime* rt) {
  struct list_head *el, *next;

  list_for_each_safe(el, next, &it->reads) {
    AsyncRead* rd = list_entry(el, AsyncRead, link);
    list_del(&rd->link);
    js_async_free_rt(rt, &rd->promise);
    js_free_rt(rt, rd);
  }
}

AsyncIterator*
asynciterator_new(JSContext* ctx) {
  AsyncIterator* iter;

  if((iter = js_malloc(ctx, sizeof(AsyncIterator))))
    asynciterator_zero(iter);
  return iter;
}

void
asynciterator_free(AsyncIterator* it, JSRuntime* rt) {
  if(--it->ref_count == 0) {
    asynciterator_clear(it, rt);
    js_free_rt(rt, it);
  }
}

JSValue
asynciterator_next(AsyncIterator* it, JSContext* ctx) {
  AsyncRead* rd;
  JSValue ret = JS_UNDEFINED;

  if(it->closed)
    return JS_ThrowInternalError(ctx, "%s: iterator closed", __func__);

  if((rd = js_malloc(ctx, sizeof(AsyncRead)))) {
    list_add(&rd->link, &it->reads);
    ret = js_async_create(ctx, &rd->promise);
    rd->id = ++it->serial;
  }

  asynciterator_check_closing(it, ctx);

  return ret;
}

BOOL
asynciterator_stop(AsyncIterator* it, JSContext* ctx) {

  if(!list_empty(&it->reads)) {
    asynciterator_emplace(it, JS_UNDEFINED, TRUE, ctx);
    it->closed = TRUE;

    asynciterator_cancel(it, JS_NULL, ctx);
    return TRUE;
  } else {
    it->closing = TRUE;
  }

  return FALSE;
}

int
asynciterator_cancel(AsyncIterator* it, JSValueConst error, JSContext* ctx) {
  int ret = 0;
  AsyncRead* rd;

  while((rd = asynciterator_shift(it, ctx))) {
    js_async_reject(ctx, &rd->promise, error);
    js_free(ctx, rd);
    ret++;
  }

  it->closed = TRUE;

  return ret;
}

BOOL
asynciterator_emplace(AsyncIterator* it, JSValueConst value, BOOL done, JSContext* ctx) {
  AsyncRead* rd;

  if((rd = asynciterator_shift(it, ctx))) {

    // printf("%-22s reads: %zu read: %" PRIu32 " value: %.*s done: %i\n", __func__, list_size(&it->reads), rd->id, 10, JS_ToCString(ctx, value), done);

    JSValue obj = asynciterator_object(value, done, ctx);
    js_async_resolve(ctx, &rd->promise, obj);
    JS_FreeValue(ctx, obj);
    js_free(ctx, rd);
    //    it->serial++;
    return TRUE;
  }

  return FALSE;
}

JSValue
asynciterator_object(JSValueConst value, BOOL done, JSContext* ctx) {
  JSValue obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, obj, "value", JS_DupValue(ctx, value));
  JS_SetPropertyStr(ctx, obj, "done", JS_NewBool(ctx, done));

  return obj;
}

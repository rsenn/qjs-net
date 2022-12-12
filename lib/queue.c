#include "queue.h"
#include <assert.h>

void
queue_zero(Queue* q) {
  init_list_head(&q->items);
  q->size = 0;
}

void
queue_clear(Queue* q, JSContext* ctx) {
  queue_clear_rt(q, JS_GetRuntime(ctx));
}

void
queue_clear_rt(Queue* q, JSRuntime* rt) {
  struct list_head *p, *p2;

  list_for_each_safe(p, p2, &q->items) {
    QueueItem* i = list_entry(p, QueueItem, link);

    list_del(p);
    block_free_rt(&i->block, rt);
    // JS_FreeValueRT(rt, i->value);

    free(p);
  }

  q->size = 0;
}

void
queue_free(Queue* q, JSContext* ctx) {
  queue_clear(q, ctx);
  js_free(ctx, q);
}

Queue*
queue_new(JSContext* ctx) {
  Queue* q;

  if((q = js_malloc(ctx, sizeof(Queue))))
    queue_zero(q);

  return q;
}

QueueItem*
queue_front(Queue* q) {
  return list_empty(&q->items) ? 0 : list_entry(q->items.next, QueueItem, link);
}

QueueItem*
queue_back(Queue* q) {
  return list_empty(&q->items) ? 0 : list_entry(q->items.prev, QueueItem, link);
}

ByteBlock
queue_next(Queue* q, BOOL* done_p) {
  ByteBlock ret = {0, 0};
  QueueItem* i;
  BOOL done = FALSE;

  if((i = queue_front(q))) {
    ret = i->block;
    done = i->done;

    if(i->resolve) {
      JSContext* ctx = deferred_getctx(i->resolve);
      JSValue fn = deferred_getjs(i->resolve);

      JS_FreeValue(ctx, JS_Call(ctx, fn, JS_UNDEFINED, 0, 0));

      DoubleWord retval = deferred_call(i->resolve);
      JS_FreeValue(i->resolve->args[0], retval.js);
      deferred_free(i->resolve);
    }

    if(!done) {
      list_del(&i->link);

      --q->size;
      free(i);
    }
  }

  if(done_p)
    *done_p = done;

  return ret;
}

QueueItem*
queue_put(Queue* q, ByteBlock chunk) {
  QueueItem* i;

  assert(!queue_closed(q));

  if(q->items.next == 0 && q->items.prev == 0)
    init_list_head(&q->items);

  if((i = malloc(sizeof(QueueItem)))) {
    i->block = chunk;
    i->done = FALSE;
    i->resolve = 0;
    i->unref = 0;

    list_add_tail(&i->link, &q->items);

    ++q->size;
  }

  return i;
}

QueueItem*
queue_write(Queue* q, const void* data, size_t size, JSContext* ctx) {
  ByteBlock chunk = block_copy(data, size, ctx);

  return queue_put(q, chunk);
}

QueueItem*
queue_close(Queue* q) {
  QueueItem* i;

  assert(!queue_closed(q));

  if(q->items.next == 0 && q->items.prev == 0)
    init_list_head(&q->items);

  if((i = malloc(sizeof(QueueItem)))) {
    i->block = (ByteBlock){0, 0};
    i->done = TRUE;
    i->resolve = 0;
    i->unref = 0;

    list_add_tail(&i->link, &q->items);
  }

  return i;
}

int64_t
queue_bytes(Queue* q) {
  QueueItem* i;
  struct list_head* el;
  size_t bytes = 0;

  if(!queue_complete(q))
    return -1;

  list_for_each(el, &q->items) {
    i = list_entry(el, QueueItem, link);

    bytes += block_SIZE(&i->block);
  }

  return bytes;
}

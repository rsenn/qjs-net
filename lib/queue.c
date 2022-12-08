#include "queue.h"

void
queue_zero(Queue* q) {
  init_list_head(&q->items);
}

void
queue_free(Queue* q, JSContext* ctx) {
  struct list_head *p, *p2;

  list_for_each_safe(p, p2, &q->items) {
    QueueItem* i = list_entry(p, QueueItem, link);

    list_del(p);
    JS_FreeValue(ctx, i->value);

    free(p);
  }

  js_free(ctx, q);
}

Queue*
queue_new(JSContext* ctx) {
  Queue* q;

  if((q = js_malloc(ctx, sizeof(Queue))))
    queue_zero(q);

  return q;
}

JSValue
queue_next(Queue* q, BOOL* done_p) {
  JSValue ret = JS_UNDEFINED;
  QueueItem* i;
  BOOL done = FALSE;

  if(!list_empty(&q->items)) {
    i = list_entry(q->items.next, QueueItem, link);

    list_del(&i->link);

    ret = i->value;
    done = i->done;

    if(i->resolve) {
      DoubleWord retval = deferred_call(i->resolve);
      JS_FreeValue(i->resolve->args[0], retval.js);
      deferred_free(i->resolve);
    }

    free(i);
  }

  if(done_p)
    *done_p = done;

  return ret;
}

QueueItem*
queue_put(Queue* q, JSValueConst value) {
  QueueItem* i;

  if((i = malloc(sizeof(QueueItem)))) {
    i->value = value;
    i->done = FALSE;

    list_add_tail(&i->link, &q->items);
  }

  return i;
}

void
queue_close(Queue* q) {
  QueueItem* i;

  if((i = malloc(sizeof(QueueItem)))) {
    i->value = JS_UNINITIALIZED;
    i->done = TRUE;

    list_add_tail(&i->link, &q->items);
  }
}

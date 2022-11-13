#include "queue.h"

struct item {
  JSValue value;
  BOOL done;
  struct list_head link;
};

void
queue_zero(struct queue* q) {
  init_list_head(&q->items);
}

void
queue_free(struct queue* q, JSContext* ctx) {
  struct list_head *p, *p2;

  list_for_each_safe(p, p2, &q->items) {
    struct item* i = list_entry(p, struct item, link);

    list_del(p);
    JS_FreeValue(ctx, i->value);

    free(p);
  }

  js_free(ctx, q);
}

struct queue*
queue_new(JSContext* ctx) {
  struct queue* q;

  if((q = js_malloc(ctx, sizeof(struct queue))))
    queue_zero(q);

  return q;
}

JSValue
queue_next(struct queue* q, BOOL* done_p) {
  JSValue ret = JS_UNDEFINED;
  struct item* i;
  BOOL done = FALSE;

  if(!list_empty(&q->items)) {
    i = (struct item*)q->items.next;

    list_del(&i->link);

    ret = i->value;
    done = i->done;

    free(i);
  }

  if(done_p)
    *done_p = done;

  return ret;
}

BOOL
queue_put(struct queue* q, JSValueConst value) {
  struct item* i;

  if((i = malloc(sizeof(struct item)))) {
    i->value = value;
    i->done = FALSE;

    list_add_tail(&i->link, &q->items);
  }

  return !!i;
}

void
queue_close(struct queue* q) {
  struct item* i;

  if((i = malloc(sizeof(struct item)))) {
    i->value = JS_UNINITIALIZED;
    i->done = TRUE;

    list_add_tail(&i->link, &q->items);
  }
}

#include "queue.h"

void
queue_zero(struct queue* q) {
  init_list_head(&q->items);
}

void
queue_destroy(struct queue** gen_p) {
  struct queue* q;

  if((q = *gen_p)) {
    if(queue_free(q))
      *gen_p = 0;
  }
}

BOOL
queue_free(struct queue* q) {
  if(--q->ref_count == 0) {
    return TRUE;
  }
  return FALSE;
}

struct queue*
queue_new(JSContext* ctx) {
  struct queue* q;

  if((q = js_malloc(ctx, sizeof(struct queue)))) {
    queue_zero(q);
  }
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

int
queue_put(struct queue* q, JSValueConst value) {
  struct item* i;

  if((i = malloc(sizeof(struct item)))) {
    i->value = value;
    i->done = FALSE;

    list_add_tail(&i->link, &q->items);
    return 1;
  }
  return 0;
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

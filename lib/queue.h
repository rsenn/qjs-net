#ifndef QUICKJS_NET_LIB_QUEUE_H
#define QUICKJS_NET_LIB_QUEUE_H

#include <quickjs.h>
#include <list.h>
#include <cutils.h>

struct queue {
  struct list_head items;
};

void queue_zero(struct queue*);
void queue_free(struct queue*, JSContext*);
struct queue* queue_new(JSContext*);
JSValue queue_next(struct queue*, BOOL* done_p);
int queue_put(struct queue*, JSValueConst value);
void queue_close(struct queue*);

static inline BOOL
queue_empty(struct queue* q) {
  return list_empty(&q->items);
}

#endif /* QUICKJS_NET_LIB_QUEUE_H */

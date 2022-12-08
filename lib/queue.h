#ifndef QUICKJS_NET_LIB_QUEUE_H
#define QUICKJS_NET_LIB_QUEUE_H

#include <quickjs.h>
#include <list.h>
#include <cutils.h>
#include "deferred.h"

typedef struct queue {
  struct list_head items;
} Queue;

typedef struct queue_item {
  struct list_head link;
  JSValue value;
  BOOL done;
  Deferred* resolve;
} QueueItem;

void queue_zero(Queue*);
void queue_free(Queue*, JSContext* ctx);
Queue* queue_new(JSContext*);
JSValue queue_next(Queue*, BOOL* done_p);
QueueItem* queue_put(Queue*, JSValueConst value);
void queue_close(Queue*);

static inline BOOL
queue_empty(Queue* q) {
  return list_empty(&q->items);
}

#endif /* QUICKJS_NET_LIB_QUEUE_H */

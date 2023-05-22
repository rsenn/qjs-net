/**
 * @file queue.h
 */
#ifndef QJSNET_LIB_QUEUE_H
#define QJSNET_LIB_QUEUE_H

#include "buffer.h"
#include "js-utils.h"
#include "deferred.h"

typedef struct queue {
  struct list_head items;
  size_t size;
  BOOL continuous;
} Queue;

typedef struct queue_item {
  struct list_head link;
  ByteBlock block;
  BOOL done;
  Deferred* unref;
} QueueItem;

void queue_zero(Queue*);
void queue_clear(Queue*, JSRuntime* rt);
void queue_free(Queue*, JSRuntime* ctx);
Queue* queue_new(JSContext*);
QueueItem* queue_front(Queue*);
QueueItem* queue_back(Queue*);
QueueItem* queue_last_chunk(Queue*);
ByteBlock queue_next(Queue*, BOOL* done_p);
QueueItem* queue_add(Queue*, ByteBlock chunk);
QueueItem* queue_put(Queue*, ByteBlock chunk, JSContext* ctx);
QueueItem* queue_write(Queue*, const void* data, size_t size, JSContext* ctx);
QueueItem* queue_close(Queue*);
size_t queue_bytes(Queue*);
QueueItem* queue_continuous(Queue* q);

static inline BOOL
queue_empty(Queue* q) {
  return list_empty(&q->items);
}

static inline BOOL
queue_closed(Queue* q) {
  QueueItem* i;
  return (i = queue_front(q)) ? i->done : FALSE;
}

static inline BOOL
queue_complete(Queue* q) {
  QueueItem* i;
  return (i = queue_back(q)) ? i->done : FALSE;
}

static inline size_t
queue_size(Queue* q) {
  return q->size;
}

#endif /* QJSNET_LIB_QUEUE_H */

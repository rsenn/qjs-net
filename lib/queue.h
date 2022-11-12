#ifndef QUICKJS_NET_LIB_QUEUE_H
#define QUICKJS_NET_LIB_QUEUE_H

#include <quickjs.h>

struct queue {
  struct list_head items;
};

struct item {
  JSValue value;
  BOOL done;
  struct list_head link;
};

void queue_zero(struct queue*);
void queue_destroy(struct queue**);
BOOL queue_free(struct queue*);
struct queue* queue_new(JSContext*);
JSValue queue_next(struct queue*, BOOL* done_p);
int queue_put(struct queue*, JSValueConst value);
void queue_close(struct queue*);

#endif /* QUICKJS_NET_LIB_QUEUE_H */

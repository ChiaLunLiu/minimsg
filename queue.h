#ifndef __QUEUE_H__
#define __QUEUE_H__

#define QUEUE_OK 0
#define QUEUE_FAIL 1

struct _queue;
typedef struct _queue queue_t;
struct _qelement;
typedef struct _qnode qnode_t;

struct _qnode{
	qnode_t * next;
	void* data;
};

struct _queue{
	qnode_t * front;
	qnode_t * end;
	int number;
};
inline int queue_size(queue_t* q);
queue_t* queue_alloc( );
void queue_free(queue_t* q);
int queue_push(queue_t* q,void* data);
void* queue_pop(queue_t* q);
#endif

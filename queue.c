#include "queue.h"
#include<stdio.h>
#include<stdlib.h>
queue_t* queue_alloc(  )
{
	queue_t * q = malloc( sizeof(queue_t));
	if(!q)return NULL;
	q->front = q->end = NULL;
	q->number = 0;
	return q;
}
void queue_free(queue_t* q)
{
	/* memory leak of queue element */
	free(q);
}
int queue_push(queue_t* q,void* data)
{
	qnode_t * qn;
	qn = (qnode_t*) malloc( sizeof( qnode_t ));
	if(!qn)return QUEUE_FAIL;
	
	qn->data =data;
	qn->next =NULL;
	if(q->end){
		q->end->next = qn;
		q->end = qn;
	}
	else{
		q->end = qn;
	}
	if(q->front ==NULL) q->front = qn;
	q->number++;
	return QUEUE_OK;
}
void* queue_pop(queue_t* q)
{
	qnode_t * qn;
	void* r;
	if(q->front){
		if(q->front == q->end ) q->end = NULL;
		qn = q->front;
		q->front = qn->next;
		r = qn->data;
		q->number--;
		free(qn);
		return r;
	}
	return NULL;
}
inline int queue_size(queue_t* q)
{
	return q->number;
}

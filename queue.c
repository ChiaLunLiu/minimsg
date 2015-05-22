#include "queue.h"
#include<stdio.h>
#include<stdlib.h>
queue_t* queue_alloc( void(*func)(void* ) )
{
	queue_t * q = malloc( sizeof(queue_t));
	if(!q)return NULL;
	q->front = q->end = NULL;
	q->free_func = func;
	q->number = 0;
	return q;
}
void queue_free(queue_t* q)
{
	qnode_t* qn,*tmp;
	qn = q->front;
	for(qn = q->front ; qn ; ){
		tmp = qn;
		qn = qn->next;
		q->free_func( tmp->data);
		free(tmp);
	}
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
	if(q->front){
		if(q->front == q->end ) q->end = NULL;
		qn = q->front;
		q->front = qn->next;
		qn->next= NULL;
		q->number--;
		return qn;
	}
	return NULL;
}

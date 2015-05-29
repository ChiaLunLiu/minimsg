/*
 * 
 * thread_pool_add_
 */
#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__
#include<pthread.h>
#include "queue.h"
struct _thpool;
typedef struct _thnode{
	pthread_t pth;
	queue_t* q;
	int active; 
	int efd; /* event fd */
	pthread_spinlock_t qlock; /* queue lock */
	struct _thpool* thp;

	int id;
}thnode_t;
typedef struct _thpool
{
	int thread_num;
	thnode_t ** threads;
	int efd;
	queue_t* q;
	pthread_spinlock_t qlock;
}thpool_t;
/* thnode API */
thnode_t* thread_create( );
void thread_destroy(thnode_t* );
/* thpool_t API */
thpool_t* thread_pool_alloc(int num);
/* schedule_task would allocate task to a thread with least jobs */
void thread_pool_schedule_task(thpool_t* thp, void*(*)(void*), void* arg);
/* assign_task assigns task to specified thread */
void thread_pool_assign_task(thpool_t* thp, void*(*)(void*), void* arg,int no);
void thread_pool_destroy(thpool_t* thp);
#endif

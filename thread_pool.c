#include "thread_pool.h"
#include "util.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

typedef struct _qelement_t
{
	void*(*task)(void* arg); /* task function should free arg */
	void* arg;
}qelement_t;

/*
 *  after kill_task is called, we should not insert any task to queue
 */
void* kill_task(void* arg)
{
	thnode_t* th = (thnode_t*)arg;
	printf("%s(%d)\n",__func__,th->id);
	/* pool would free the thread resource */
	th->active = 0;
	return NULL;
}


static void* do_task(void* arg)
{
	int i;
	uint64_t ur;
	uint64_t uw;
	void* r;
    	ssize_t s;

	thnode_t* th = (thnode_t*)arg;		
	qelement_t * qe;
	while(th->active){
		s = read(th->efd, &ur, sizeof(uint64_t));
        	if (s != sizeof(uint64_t)){
			th->active = 0;
			continue;
		}
        	for(i = 0; th->active && i< ur ; i++){
			pthread_spin_lock(&th->qlock);
				qe = (qelement_t*) queue_pop( th->q); 
			pthread_spin_unlock(&th->qlock);
			if(!qe){
						/* this shouldn't happen */
						handle_error("queue is null");
			}
			else{
				if(qe->task == NULL) fprintf(stderr,"null task\n");
				else{
					r=qe->task(qe->arg);
					if(r){
						pthread_spin_lock(&th->thp->qlock);
						  queue_push( th->thp->q,r); 
						pthread_spin_unlock(&th->thp->qlock);

						uw = 1;
						/* write is thread safe */
    						s = write(th->thp->efd, &uw, sizeof(uint64_t));
    						if (s != sizeof(uint64_t)) handle_error("write");
					}			
				}
				free(qe);
			}
		}
	}
	
	
	printf("leave ...\n");	
	return NULL;
}
thnode_t* thread_create( )
{
	int s;
	thnode_t* th;
	th = malloc( sizeof( thnode_t ));
	if(!th) handle_error("malloc thnode_t");
	
	th->q = queue_alloc();
	if(!th->q)handle_error("malloc queue_t");
		
	th->efd = eventfd(0,0);
	if(th->efd == -1) handle_error("eventfd");
	
	if(pthread_spin_init(&th->qlock,0)!=0) handle_error("pthread_spin_init");

	th->active = 1;
	s = pthread_create(&th->pth,NULL,do_task,(void*)th);
	if(s != 0) handle_error("pthread_create");
	
	return th;
}
void thread_destroy(thnode_t* th)
{	
	/* wait until task is complete */
	pthread_spin_destroy(&th->qlock);
	close(th->efd);	
	queue_free(th->q);
	free(th);
}
thpool_t* thread_pool_alloc(int num )
{
	thpool_t *thp;
	int i;
	thp = malloc( sizeof(thpool_t ));
	if(!thp)return NULL;
	
	thp->thread_num = num;
	
	thp->threads = malloc( sizeof( thnode_t) * num );
	
	if(!thp->threads)return NULL;
	for(i = 0;i<num ; i++){
		thp->threads[i] = thread_create();
		thp->threads[i]->thp = thp;
		thp->threads[i]->id = i;
	}

	thp->q = queue_alloc();
	if(!thp->q)handle_error("malloc queue_t");
	
	thp->efd = eventfd(0,0);
        if(thp->efd == -1) handle_error("eventfd");

        if(pthread_spin_init(&thp->qlock,0)!=0) handle_error("pthread_spin_init");

	return thp;
}
/* 
 * rough way to find a thread with lighest load 
 *  based on number of assigned task
 */
void thread_pool_schedule_task(thpool_t* thp, void*(*task)(void*), void* arg)
{
	int i;
	int idx=-1;
	int sz;
	int tmp;
	for(i = 0 ;i<thp->thread_num ; i++){
		tmp =  queue_size( thp->threads[i]->q);
		if(idx == -1|| sz > tmp){
			idx = i;
			sz = tmp;
		}
	}
	thread_pool_assign_task(thp,task,arg,idx);
}

void thread_pool_assign_task(thpool_t* thp, void*(*task)(void*), void* arg,int no)
{
	thnode_t* th = (thnode_t*)thp->threads[no];
	uint64_t u;
    ssize_t s;
	dbg("assign task to %d\n",no);
	qelement_t* qe = malloc( sizeof(qelement_t));
	
	if(!qe)	handle_error("malloc qelement_t");
	qe->task = task;
	qe->arg = arg;
	
	pthread_spin_lock(&th->qlock);
		queue_push( th->q,(void*)qe); 
	pthread_spin_unlock(&th->qlock);
	u = 1;
    	s = write(th->efd, &u, sizeof(uint64_t));
    	if (s != sizeof(uint64_t)) handle_error("write");
//	printf("add task to %d\n",no);	
}

void thread_pool_destroy(thpool_t* thp)
{
	int i;
	/* send kill task to all threads  */

	for(i= 0;i<thp->thread_num ; i++){
		thread_pool_assign_task(thp,kill_task,(void*)thp->threads[i],i);
	}

	for(i = 0;i<thp->thread_num ; i++){
		printf("pending task for thread (%d) : %d\n",i,thp->threads[i]->q->number);
//		pthread_kill( thp->threads[i]->pth);
		/* close fd first , so that thread won't block on reading */
		pthread_join( thp->threads[i]->pth,NULL);
		thread_destroy(thp->threads[i]);
		printf("thread %d is destroyed\n",i);
	}
	pthread_spin_destroy(&thp->qlock);
        close(thp->efd);
        queue_free(thp->q);
	free(thp->threads);
	free(thp);
} 

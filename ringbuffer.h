#ifndef __RINGBUFFER_H__
#define __RINGBUFFER_H__

#define RINGBUFFER_OK 1
#define RINGBUFFER_FAIL 0
typedef struct _ringbuffer{
	/* free_space to test full or empty */
	int free_space; 
	/* max length of ringbuffer */
	int length; 
	/* [start,end) */
	int start; /* the beginning of the data */
	int end; /* the next position */
	char buf[0];
}ringbuffer_t;

ringbuffer_t * ringbuffer_alloc(int sz);
void ringbuffer_destroy(ringbuffer_t* rb);

/* push [ptr,ptr+sz) to ringbuffer */
int ringbuffer_push_data(ringbuffer_t* rb,char *ptr,int sz);
/* copy ringbuffer to [ptr,ptr+sz) */
int ringbuffer_pop_data(ringbuffer_t * rb,char* ptr,int sz);
/* total data size */
int ringbuffer_datasize(const ringbuffer_t* rb);
/* total free size */
int ringbuffer_freesize(const ringbuffer_t* rb);
/* linear free size */
int ringbuffer_linear_freesize(const ringbuffer_t* rb);
int ringbuffer_linear_datasize(const ringbuffer_t* rb);
/* end ptr */
char* ringbuffer_get_end(const ringbuffer_t* rb);
/* start ptr */
char* ringbuffer_get_start(const ringbuffer_t* rb);
/* buffer full ? */
int ringbuffer_is_full(const ringbuffer_t* rb);
/* buffer empty */
int ringbuffer_is_empty(const ringbuffer_t* rb);
void ringbuffer_print(const ringbuffer_t *rb);
#endif

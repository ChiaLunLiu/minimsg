#include "ringbuffer.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
ringbuffer_t * ringbuffer_alloc(int sz)
{
	ringbuffer_t* rb;
	rb = malloc( sizeof( ringbuffer_t)  + sz);
	if(!rb)return NULL;
	rb->free_space = rb->length = sz;
	rb->start = rb->end = 0;
	return rb;
}
void ringbuffer_destroy(ringbuffer_t* rb)
{
	if(rb)free(rb);
}
char* ringbuffer_get_end(const ringbuffer_t* rb)
{
	return (char*)rb->buf + rb->end;
}
int ringbuffer_is_full(const ringbuffer_t* rb)
{
	return rb->free_space == 0;
}
int ringbuffer_linear_freesize(const ringbuffer_t* rb)
{
	return (rb->end > rb->start) ? rb->length - rb->end : rb->start - rb->end;
}
int ringbuffer_linear_datasize(const ringbuffer_t* rb)
{
	return (rb->start < rb->end) ? rb->end - rb->start : rb->length - rb->start;
}
int ringbuffer_freesize(const ringbuffer_t* rb)
{
	return rb->free_space;
}
int ringbuffer_datasize(const ringbuffer_t* rb)
{
	return rb->length - rb->free_space;
}
int ringbuffer_push_data(ringbuffer_t* rb,char *ptr,int sz)
{
	int copy;
	if(ringbuffer_freesize(rb) >= sz){
		if(rb->end >= rb->start){
			if(rb->length - rb->end >= sz){
				memcpy(rb->buf + rb->end , ptr, sz);
			}
			else{
				copy = rb->length - rb->end;
				memcpy(rb->buf+rb->end,ptr,copy);
				memcpy(rb->buf, ptr+copy,sz - copy);
			}
		}
		else{
			memcpy(rb->buf + rb->end, ptr,sz);
		}
		rb->free_space-=sz;
		rb->end+=sz;
		if(rb->end >= rb->length )rb->end -=rb->length;
		return RINGBUFFER_OK;
	}
	return RINGBUFFER_FAIL;
}
int ringbuffer_pop_data(ringbuffer_t * rb,char* ptr,int sz)
{
	int copy;
	if( ringbuffer_datasize(rb) >= sz){
		if( rb->end > rb->start){
			memcpy(ptr,rb->buf + rb->start , sz); 										
		}
		else{
			if(rb->length - rb->start >= sz){
				memcpy(ptr,rb->buf + rb->start, sz);
			}
			else{
				copy = rb->length - rb->start;
				memcpy(ptr,rb->buf+rb->start,copy);
				memcpy(ptr+copy,rb->buf,sz - copy);
			}
		}
		rb->start+=sz;
		rb->free_space+=sz;
		if(rb->start >= rb->length ) rb->start -= rb->length;
		return RINGBUFFER_OK;		
	}	
	return RINGBUFFER_FAIL;
}
char* ringbuffer_get_start(const ringbuffer_t* rb)
{
	return (char*)rb->buf + rb->start;
}
int ringbuffer_is_empty(const ringbuffer_t* rb)
{
	return rb->free_space == rb->length ;
}
void ringbuffer_print(const ringbuffer_t*rb)
{
	int i;
	printf("%s\n",__func__);
	if(rb->end > rb->start){
		printf("[%d,%d]:",rb->start,rb->end-1);
		for(i = rb->start ; i!= rb->end ; i++)
			printf("%c",rb->buf[ i]);
		printf("\n");
	}
	else{
		printf("[%d,%d]:",rb->start,rb->length-1);
		for(i = rb->start ; i!= rb->length ; i++)
			printf("%c",rb->buf[ i]);
		printf("\n");
		if(rb->end > 0){
			printf("[%d,%d]:",0,rb->end-1);
			for(i = 0 ; i!= rb->end ; i++)
				printf("%c",rb->buf[ i]);
			printf("\n");
		}
		printf("\n");
	}
}

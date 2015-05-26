#ifndef __MINIMSG_H__
#define __MINIMSG_H__
#include "ringbuffer.h"
#include "queue.h"
#include <event2/event.h>
#define MINIMSG_OK 2
#define MINIMSG_FAIL 3
#define MINIMSG_SEND_COMPLETE 4
#define MINIMSG_SEND_BLOCK 5
#define MINIMSG_MSGSERVER_BUFFER_SIZE 2048

/* fsm for non-blocking send */
#define MINIMSG_STATE_SEND_NUMBER_OF_FRAME 0
#define MINIMSG_STATE_SEND_FRAME_LENGTH 1
#define MINIMSG_STATE_SEND_FRAME_CONTENT 2
/* fsm for non-blocking recv */
#define MINIMSG_STATE_RECV_NUMBER_OF_FRAME 0
#define MINIMSG_STATE_RECV_FRAME_LENGTH 1
#define MINIMSG_STATE_RECV_FRAME_CONTENT 2
#define MINIMSG_MAX_NUMBER_OF_FRAME 128
#define MINIMSG_MAX_FRAME_CONTENT_SIZE 2048
#define MINIMSG_RINGBUFFER_SIZE (2*MINIMSG_MAX_FRAME_CONTENT_SIZE)

struct frame;
typedef struct frame frame_t;


struct frame
{
	/* used only in msg API */
	frame_t* next;
	
	/* address of length should be 4 byte aligned because
	 * length and content are assumed to be placed without align gap
         */
	int cur; /* current content size */
	int length; /* max content size */
	char content[0];
};

typedef struct msg
{
	/* length : size of all frames */
	int length; /* currently not used */
	int frames;
	/* linked list structure */
	queue_t* q;
	frame_t* front;
	frame_t* end;	
}msg_t;

typedef struct _msg_state{
	int sock;
    	struct event *read_event;
    	struct event *write_event;
	ringbuffer_t* rb_recv;
	ringbuffer_t* rb_send;
	/* state recv */
	int       recv_state;
	frame_t * recv_frame; /* current processing frame */
	int       recv_number_of_frame;
	int       recv_current_frame_byte;/* current frame data that has been recv */
	msg_t *   recv_msg;	
	/* state send */
	int 	 send_state;
	const char* send_ptr;
	frame_t* send_frame;
	int 	 send_byte;
	int 	 send_content_byte;
	msg_t*   send_msg;
	queue_t* send_q;
	int      send_buf;
}fd_state_t;




/* ----------------------
 * frame API
 * ---------------------- 
 */
 int frame_truesize(const frame_t* f);
int frame_transfer_byte(const frame_t* f);
const char* frame_content(const frame_t* f);
int frame_send(int sock,frame_t* f);
/* 
 * frame_recv would allocate memory for frame 
 * when the return value is
 *   MINIMSG_OK   : 
 *   MINIMSG_FAIL : recv <=0 , the caller should close the fd by itself.
 */
int frame_recv(int sock,frame_t** f);
frame_t* frame_alloc(int sz);
/* '\0' is not appended */
frame_t* frame_string(const char* str);
void	 frame_free(frame_t* f);

/* ----------------------
 * msg API
 * ---------------------- 
 */
/* get the current first frame */
frame_t* msg_pop_frame(msg_t* m);
frame_t* msg_front_frame(msg_t* m);
msg_t* msg_alloc();
void msg_free(msg_t* m);
void msg_print(const msg_t * m);
int msg_append_string(msg_t* m,const char* str);
void msg_append_frame(msg_t* m,frame_t* f);
/* blocking API */
int msg_send(int sock,msg_t* m);
int msg_recv(int sock,msg_t** m);
/* non-blocking API */
/* send message stored in fds-> m */


/* other */
void do_accept(evutil_socket_t listener, short event, void *arg);
#endif

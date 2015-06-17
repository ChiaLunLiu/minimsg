/*
 * 
 * 
 *  when to free fd_state_t is timing issue, 
 *  when the socket works as server,
 *  do_accept : +1
 *  push to ctx_recv_q : +1
 *  assign socket->current: +1
 *  drop socket->current: -1
 *  pop from ctx_recv_q : -1
 *  network disconnect : -1
 * 
 *  When the socket works as client,
 *  socket create : +1
 *  network connect : +1
 *  network disconnect : -1  
 *  socket destroy : -1
 */
#ifndef __MINIMSG_H__
#define __MINIMSG_H__
#include "ringbuffer.h"
#include "thread_pool.h"
#include "queue.h"
#include "util.h"
#include "list.h"
#include <event2/event.h>
#include <pthread.h>
#include <sys/types.h>
#define MINIMSG_AF_UNIX 0
#define MINIMSG_AF_INET 1

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

#define MINIMSG_REQ 0
#define MINIMSG_REP 1

#define MINIMSG_SOCKET_STATE_RECV 0 
#define MINIMSG_SOCKET_STATE_SEND 1
#define MINIMSG_SOCKET_STATE_BOTH 2
#define MINIMSG_NEW 0
#define MINIMSG_CONNECTING 1
#define MINIMSG_CONNECTED 2

struct frame;
typedef struct frame frame_t;
struct _msg_server;
typedef struct _msg_server msg_server_t;
struct _minimsg_context;
struct _minimsg_socket;
typedef struct _minimsg_socket minimsg_socket_t;


typedef struct event_arg
{
	void*base; /* event base */
	int create_thread; /* 0: don't call thread to handle msg ; > 0: create thread */
}event_arg_t;


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
	minimsg_socket_t* minimsg_socket; /* 
									   * each instance pertains to a minimsg_socket
									   *  For client minimsg_socket, it has only one instace
									   *  For server minimsg_socket, its instances are based
									   *  number of connection
									   */
	list_node_t* list_node;           /* linked in minimsg_context_t connected list */
	int refcnt;
	pthread_spinlock_t lock;          /* lock for reference counter */	
}fd_state_t;

struct _msg_server{
	int sock; /* listener sock */
	thpool_t* thp;
	struct event * listener_event;
	struct event* thread_event;
	struct event* sigusr1_event;
	void*(*cb)(void*);  /* thread task's function */
	int (*task_scheduler)(const void*); /* schedule task based on message content */
	void* base;
};


typedef struct _msg_client{
//	client_info_t* info;
	pthread_t pth; /* threads for auto reconnect */
}msg_client_t;

struct _minimsg_socket{
	queue_t * pending_send_q; /* when the data can't be sent
							   * store it here, when it is connected
							   * remove all elements and add them
							   * to minimsg_context send_q 
							   */
	queue_t * recv_q;
	int recv_efd;
	int type; /* REQ, REP */
	int state ; /* recv or send, or both */
	pthread_spinlock_t lock;
	int isClient; /* 1: client ; 0: server */
	struct _minimsg_context* ctx;
	struct _minimsg_socket * next; /* for linked list */
	fd_state_t* current; /* currently processing session
						  * when isClient = 1, it is always the same session
						  * 	 isClient = 0, it is the currently processing session
						  */
	list_node_t*  list_node; /* for auto reconnect for minimsg_context */
	/* client */
	int is_connecting;         /* 0: never connect, 1: connecting ; 2 : connected */
	struct sockaddr_in server; /* server address */
	/* server */
	struct event* listener_event;
	list_t* connected_list; /* a list of connected session of type fd_state_t */
	evutil_socket_t listener; /* server_sock */ 
	
};
typedef struct _minimsg_context{
	void * base;
	pthread_t thread;            /* network I/O thread */
	
	/* the following 3 variables builds event that sends control message to network I/O thread */
	queue_t* control_q;
	int control_efd;  
	struct event* control_event; 
	
	/* the following 3 variables builds event that sends data messages to network */
	queue_t* data_q;
	int data_efd;
	struct event* data_event;   
	
	pthread_spinlock_t lock;
	struct event* timeout_event;  /* for auto connect */
	list_t* connecting_list;      /* a list of connecting socket for auto connect*/
	list_t* connected_list;       /* a list of connected fd_state_t */
}minimsg_context_t;

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
int msg_number_of_frame(const msg_t* m);
const char* msg_content_at_frame(const msg_t* m,int idx);
frame_t* msg_pop_frame(msg_t* m);
msg_t* msg_alloc();
void msg_free(msg_t* m);
void msg_print(const msg_t * m);

void msg_prepend_frame(msg_t* m,frame_t* f);
int msg_prepend_string(msg_t* m,const char* str);
void msg_prepend_string_f(msg_t* m,const char *format, ...);
int msg_append_string(msg_t* m,const char* str);
void msg_append_string_f(msg_t* m,const char *format, ...);
void msg_append_frame(msg_t* m,frame_t* f);
/* blocking API */
int msg_send(int sock,msg_t* m);
int msg_recv(int sock,msg_t** m);
/* non-blocking API */
/* send message stored in fds-> m */


/* other */
void do_accept(evutil_socket_t listener, short event, void *arg);
msg_server_t* create_msg_server(void* base,int port);
void free_msg_server(msg_server_t* server);

msg_client_t* create_msg_clients();
int add_msg_clients(msg_client_t* c,int type,const char* location, int port,int id);
int connect_msg_clients(msg_client_t* c);
void msg_client_send(msg_client_t* clients, const char* server_location,msg_t* m);

minimsg_context_t* minimsg_create_context();
minimsg_socket_t* minimsg_create_socket(minimsg_context_t* ctx,int socket_type);
int minimsg_connect(minimsg_socket_t* s,struct sockaddr_in server);
msg_t* minimsg_recv(minimsg_socket_t* s);
/*
 *  minimsg_send
 *  the message would be free if it is sent successfully
 */
int minimsg_send(minimsg_socket_t* s, msg_t* m);

int minimsg_free_context(minimsg_context_t* ctx);
/*
 *  minimsg_free_socket
 */
int minimsg_free_socket(minimsg_socket_t* s);
 
/*
int minimsg_connect(minimsg_context_t* ctx,minimsg_socket_t* s, int minimsg_type,const char* ip, unsigned port);
int minimsg_bind(minimsg_context_t* ctx,minimsg_socket_t* s, unsigned port);


int minimsg_send(minimsg_context_t* ctx, int fd,msg_t* m);
*/
 
#endif

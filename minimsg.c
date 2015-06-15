#include "minimsg.h"
#include "queue.h"
#include "ringbuffer.h"
#include "thread_pool.h"
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
/* For sockaddr_in */
#include <netinet/in.h>
#include <netinet/tcp.h>
/* For socket functions */
#include <sys/socket.h>
/* For fcntl */
#include <fcntl.h>

#include <event2/event.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#define TIMEOUT_SECONDS 3000
typedef struct _server_thread_data{
	void* msg;
	fd_state_t* fds;
	minimsg_socket_t* server;
}server_thread_data_t;

typedef struct _queue_data{
	msg_t* msg;
	fd_state_t* fds;
}queue_data_t;


static void msg_server_read_result_from_thread_pool(evutil_socket_t fd, short events, void *arg);
static fd_state_t * alloc_fd_state(minimsg_socket_t* server, evutil_socket_t fd);
static void free_fd_state(fd_state_t *state);
static void fd_state_add_reference(fd_state_t* state);
static void free_network_rw_event(fd_state_t *state);
static void msg_recv_nb(evutil_socket_t fd, short events, void *arg);
static void msg_send_nb(evutil_socket_t fd, short events, void *arg);
static void* msg_server_thread_task_wrapper(void* arg);
static void sigusr1_func(evutil_socket_t fd, short event, void *arg);

static int check_recv_state(const minimsg_socket_t* s);
static int check_send_state(const minimsg_socket_t* s);

static void update_socket_state(minimsg_socket_t* s);
static void sending_handler(evutil_socket_t fd, short events, void *arg);
static void add_to_connecting_list(minimsg_context_t* ctx, minimsg_socket_t* s);
static void add_to_connected_list(minimsg_context_t* ctx, fd_state_t* s);


/*
 *  do_io_task 
 *  is a thread handler that does all the work inserted into event base
 *  This thread lives in minimsg_context_t and is the only thread in minimsg_context_t
 */
static void* do_io_task(void* arg);
/*
 *  timeout_cb
 *  is a timeout handler that does auto-reconnect  
 */
static void timeout_cb(int fd, short event, void *arg);



static int _frame_send(int sock,frame_t* f,int flags);

void msg_free(msg_t* m)
{
	frame_t* f,*tmp;
	for(f = m->front ; f ; ){
		tmp = f;
		f = f->next;
		frame_free(tmp);
	}
	free(m);
}

int frame_transfer_byte(const frame_t* f)
{
	return f->length + 4;
}
frame_t* frame_string(const char* str)
{
	frame_t * f;
	int len = strlen(str);
	f = frame_alloc( len +1);
	if(!f) return NULL;
	memcpy(f->content,str,len);
	f->content[len] = '\0';
	return f;
}
frame_t* frame_alloc(int sz)
{
	frame_t * f;
	f = malloc( sizeof(frame_t) + sz);
	if(!f)return NULL;
	f->length = sz;
	return f;
}
void frame_free(frame_t* f)
{
	free(f);
}
const char* frame_content(const frame_t* f)
{
	return f->content;
}
int frame_truesize(const frame_t* f)
{
	return f->length + 4;
} 
inline int frame_sendm(int sock,frame_t* f)
{
	return _frame_send(sock,f,MSG_MORE);
}
inline int frame_send(int sock,frame_t* f)
{
	return _frame_send(sock,f,0);
}
static int _frame_send(int sock,frame_t* f,int flags)
{
	int r=0;
	int sent_byte = 0;
	int total = f->length + 4;
	char*ptr;
	
//	printf("before length : %d\n",f->length);
	f->length = htonl(f->length);
//  printf("after length : %d\n",f->length);

	ptr = (char*)&(f->length);
	dbg("frame content size : %d,%d\n",total,f->length);
	while(sent_byte < total){	
		r = send(sock,ptr+sent_byte,total - sent_byte,flags);
		
		if(r == 0)return MINIMSG_FAIL;
		else if(r<0){
			if(errno == EINTR)continue;
			return MINIMSG_FAIL;
    	}
		sent_byte+=r;
	}
	return MINIMSG_OK;
}
int frame_recv(int sock,frame_t** f)
{
	int r = 0;
	int recv_byte = 0;
	int length = 0;
	char* ptr = (char*)&length;
	while(recv_byte < 4){
		r=recv(sock,ptr + recv_byte ,4 - recv_byte,0);
		if(r == 0)return MINIMSG_FAIL;
		else if(r<0){
			if(errno == EINTR)continue;
			return MINIMSG_FAIL;
    	}
    	else recv_byte+=r;
	}
	
	length = ntohl(length);
	dbg("frame length : %d\n",length);
	*f = frame_alloc( length);
	if(*f == NULL ) return MINIMSG_FAIL;
	recv_byte = 0;
	while(recv_byte < length){
		r=recv(sock,(*f)->content + recv_byte ,length - recv_byte,0);
		
		if(r == 0){
			frame_free(*f);
			return MINIMSG_FAIL;
		}
		else if(r<0){
			if(errno == EINTR)continue;
			frame_free(*f);
			return MINIMSG_FAIL;
    	}
		else recv_byte+=r;
	}
	
	/* add '\0' in the end */
//	(*f)->content[length ] ='\0';
	return MINIMSG_OK;
}
int msg_prepend_string(msg_t* m,const char* str)
{
	frame_t * f;
	f = frame_string(str);
	if(!f)return MINIMSG_FAIL;
	msg_prepend_frame(m,f);
	return MINIMSG_OK;
	
}
void msg_prepend_string_f(msg_t* m,const char *format, ...)
{
	
    va_list argptr;
    char* string;
    va_start (argptr, format);
      string = zsys_vprintf (format, argptr);
    va_end (argptr);

    if(!string) handle_error("msg_apend_string_f\n");

    msg_prepend_string(m,string); 
    free(string);
}

int msg_append_string(msg_t* m,const char* str)
{
	frame_t * f;
	f = frame_string(str);
	if(!f)return MINIMSG_FAIL;
	msg_append_frame(m,f);
	return MINIMSG_OK;
}
void msg_append_string_f(msg_t* m,const char *format, ...)
{
    va_list argptr;
    char* string;
    va_start (argptr, format);
      string = zsys_vprintf (format, argptr);
    va_end (argptr);

    if(!string) handle_error("msg_apend_string_f\n");

    msg_append_string(m,string); 
    free(string);
}
void msg_prepend_frame(msg_t* m,frame_t* f)
{
	f->next = m->front;
	m->front = f;
	if(m->end == NULL) m->end = f;
	m->frames++;
	m->length+=frame_truesize(f);
}
void msg_append_frame(msg_t* m,frame_t* f)
{
	f->next = NULL;
	if(m->end){
		m->end->next = f;
		m->end = f;
	}
	else{
		m->end = f;
	}
	if(m->front ==NULL) m->front = f;
	m->frames++;
	m->length+=frame_truesize(f);
}
msg_t* msg_alloc()
{
	msg_t* m;
	m = malloc( sizeof( msg_t));
	if(!m)return NULL;
	m->frames = 0;
	m->front = m->end = NULL;
	m->length = 0;
	return m;
}
int msg_send(int sock,msg_t* m)
{
	int r=0;
	int sent_byte = 0;
	frame_t* f;
	char* ptr;
	
	m->frames = htonl(m->frames);
	ptr= (char*)&(m->frames);

	while(sent_byte < 4){
		r = send(sock,ptr+sent_byte,4 - sent_byte,MSG_MORE);
		if(r <= 0 )break;
		sent_byte+=r;
	};
	if(r <= 0){
		msg_free(m);
		return MINIMSG_FAIL;
	}
	for(f = m->front ; f ; f = f->next){
		if(f->next == NULL){
			if(frame_send(sock,f) != MINIMSG_OK)goto fail;
		}
		else{
			if(frame_sendm(sock,f) != MINIMSG_OK) goto fail;
		}
	}
	dbg("truesize : %d\n",m->length + 4);
	msg_free(m);
	return MINIMSG_OK;
fail:
	dbg("fail to %s\n",__func__);
	//TODO
	// close socket ?
	msg_free(m);
	return MINIMSG_FAIL;
}
frame_t* msg_pop_frame(msg_t* m)
{
	frame_t* f;
	if(!m) return NULL;
	if(m->front){
		if(m->front == m->end ) m->end = NULL;
		f = m->front;
		m->front = m->front->next;
		f->next= NULL;
		m->frames--;
		return f;
	}
	return NULL;
}
int msg_recv(int sock,msg_t** m)
{
	int r = 0;
	int recv_byte = 0;
	int frames = 0;
	int i;
	frame_t* f;
	char* ptr = (char*)&frames;
	msg_t * tmp;
	dbg("[debug]: start length ...");
	while(recv_byte < 4){
		r=recv(sock,ptr + recv_byte ,4 - recv_byte,0);
		dbg("r:%d\n",r);
		
		if(r == 0)return MINIMSG_FAIL;
		else if(r<0){
			if(errno == EINTR)continue;
			return MINIMSG_FAIL;
    	}
		recv_byte+=r;
	};
	frames = ntohl(frames);
	dbg(" done\n");
	dbg("number of frames: %d\n",frames);
	tmp = msg_alloc();
	if(!tmp)return MINIMSG_FAIL;

	for(i = 0;i < frames ; i++){
		dbg("[debug] : %d\n",i);
		if(frame_recv(sock,&f) != MINIMSG_OK) goto fail;
		dbg("append\n");
		msg_append_frame(tmp,f);
	}
	*m = tmp;
	return MINIMSG_OK;
fail:
	dbg("[debug]: fail\n");
	msg_free(tmp);
	return MINIMSG_FAIL;
}
const char* msg_content_at_frame(const msg_t* m,int idx)
{
	int cnt = 0;
	frame_t* tmp;
	if(idx < m->frames){
		for(tmp = m->front ; tmp ; tmp = tmp->next , cnt++){
			if( idx == cnt ){
				return frame_content(tmp);
			}
		}
		dbg("[debug]: unexpected case \n");	
	}
	return NULL;
}

inline int msg_number_of_frame(const msg_t* m)
{
	return m->frames;
}
void msg_print(const msg_t * m)
{
	int cnt = 0;
	int i;
	const char* reply;
	frame_t * tmp;
	dbg("%s\n",__func__);
	printf("total frames: %d\n",msg_number_of_frame(m));
	for(tmp = m->front ; tmp ; tmp = tmp->next , cnt++){
		printf("[frame %d]\n",cnt);
		printf("(%d):%s\n",tmp->length,frame_content(tmp));
	}	
}
static void msg_send_nb(evutil_socket_t fd, short events, void *arg)
{
	
	int r=0;
	fd_state_t* fds = (fd_state_t*)arg;
	int sock;
	int sent_byte = 0;
	frame_t* f;
	char* ptr;
	int total;
	sock = fds->sock;
	dbg("state : %d\n",fds->send_state);

	if(fds->send_state == 0)goto stage0;
	if(fds->send_state == 1) goto stage1;
	else if(fds->send_state == 2)goto stage2;
	else if(fds->send_state == 3)goto stage3;
	else if(fds->send_state == -1){
		dbg("program shouldn't reach here, msg_send_nb is closed\n");
		return;
	}
	else{
		dbg("[bug]: unknown stage\n");
		free_fd_state(fds);
		return;
	}	
stage0:
	fds->send_state = 0;
	dbg("state0\n");
	
	fds->send_msg  = queue_pop(fds->send_q);
	if(fds->send_msg == NULL){
		dbg("nothing to send\n");
		return;
	}
	fds->send_buf = htonl(fds->send_msg->frames);
	fds->send_ptr = (char*)&(fds->send_buf);
	fds->send_byte = 0;
	dbg("%d frames to send\n",fds->send_msg->frames);	
stage1:
	fds->send_state = 1;
	dbg("state1\n");
	while(fds->send_byte < 4){
		r = send(sock,fds->send_ptr+fds->send_byte,4 - fds->send_byte,0);
		if(r <= 0 ){
				if(r == 0){
					if(errno == EPIPE){
						/* the other end closes receiving
						 * flush all sending data */
						free_network_rw_event(fds);
						free_fd_state(fds);
						return;
					}

				}
				if(errno == EINTR)continue;
				else if(errno == EAGAIN || errno ==  EWOULDBLOCK){
					event_add(fds->write_event, NULL);
					dbg("sending is blocked, add send event\n");
					return;
				}	
				else{
					/* other error 
					 * flush all sending data */
					perror("other error");
					free_network_rw_event(fds);
					free_fd_state(fds);
					return;
				}				
		}
		fds->send_byte+=r;
	};

stage2:
	fds->send_state = 2;
	dbg("state2\n");
	fds->send_frame = msg_pop_frame(fds->send_msg);
	if(fds->send_frame == NULL){
		/* msg contains 0 frames */
		msg_free(fds->send_msg);
		fds->send_msg = NULL;
		goto stage0;
	}
	fds->send_state = 3;
	fds->send_content_byte = fds->send_frame->length + 4;
	fds->send_frame->length = htonl(fds->send_frame->length);
	fds->send_byte = 0;
	fds->send_ptr = (char*)&(fds->send_frame->length);
	dbg("frame content size : %d,%d\n",fds->send_content_byte,fds->send_frame->length);
stage3:
	dbg("state3\n");
	fds->send_state = 3;
	while(fds->send_byte < fds->send_content_byte){	
		r = send(sock,fds->send_ptr+fds->send_byte,fds->send_content_byte - fds->send_byte,0);
		if(r <= 0 ){
				if(r == 0){
					if(errno == EPIPE){
						/* the other end closes receiving
						 * flush all sending data */
						free_network_rw_event(fds);
						free_fd_state(fds);
						return;
					}

				}
				if(errno == EINTR)continue;
				else if(errno == EAGAIN || errno ==  EWOULDBLOCK){
					event_add(fds->write_event, NULL);
					dbg("sending is blocked, add send event\n");
					return;
				}	
				else{
					/* other error 
					 * flush all sending data */
					perror("other error");
					free_network_rw_event(fds);
					free_fd_state(fds);
					dbg("other error\n");
					return;
				}				
		}
		fds->send_byte+=r;
	}
        frame_free(fds->send_frame);
	goto stage2;	
	return;
	
}
static void msg_recv_nb(evutil_socket_t fd, short events, void *arg)
{
	int r;
	int recv_space;
	unsigned int length;
	char* ptr;	
	ringbuffer_t * rb ;
	int copy;
	char* dst;
	queue_data_t* qdata;
	fd_state_t* fds = (fd_state_t*)arg;
	rb = fds->rb_recv;
	int leave ;
	int result;
	char buf[256];
	uint64_t u;
    ssize_t s;
	minimsg_socket_t* server = fds->minimsg_socket;
	dbg("%s\n",__func__);
	
	
	while(1){
		
		copy = ringbuffer_freesize(rb);
		if(copy > 256) copy = 256;
		result=recv(fds->sock,buf,copy,0);
		if(result<=0){
			if(result <0 && errno == EINTR)continue;
			//if(result < 0)	perror("msg_recv_nb recv");
			break;
		}
		dbg("push %d bytes\n",result);
		ringbuffer_push_data(rb,buf,result);
		
		
		leave=0;
	  while(!leave){
		  r = ringbuffer_datasize(rb);
		  if(r == 0)break;
	  switch(fds->recv_state){
		case MINIMSG_STATE_RECV_NUMBER_OF_FRAME:
			dbg("state => MINIMSG_STATE_RECV_NUMBER_OF_FRAME\n");
			if(r >= 4){
				ringbuffer_pop_data(rb,(char*)&length,4);		
				length = ntohl(length);
				if(length > MINIMSG_MAX_NUMBER_OF_FRAME){
					fprintf(stderr,"exceeds max frame %d > %d\n",length,MINIMSG_MAX_NUMBER_OF_FRAME);
					goto fail;
				}
				else dbg("get %d frames\n",length);
				fds->recv_msg  = msg_alloc();
				if(!fds->recv_msg){
					dbg("fail to msg_alloc()\n");
					goto fail;
				}
				fds->recv_number_of_frame = length;
				fds->recv_state = MINIMSG_STATE_RECV_FRAME_LENGTH;
			}
			else leave=1;
		break;
		case MINIMSG_STATE_RECV_FRAME_LENGTH:
			dbg("state => MINIMSG_STATE_RECV_FRAME_LENGTH\n");
			if(r >= 4){
				ringbuffer_pop_data(rb,(char*)&length,4);
			//	printf("before length : %d\n",length);		
				length = ntohl(length);
				if(length > MINIMSG_MAX_FRAME_CONTENT_SIZE){
					fprintf(stderr,"length content too big %d > %d\n",length,MINIMSG_MAX_FRAME_CONTENT_SIZE);
					goto fail;
				}
				else{
					dbg("length = %d\n",length);
				}
				fds->recv_frame = frame_alloc( length);
				if(!fds->recv_frame) goto fail;
				fds->recv_current_frame_byte = 0;
				fds->recv_state = MINIMSG_STATE_RECV_FRAME_CONTENT;
				/* special handling for length = 0 */
				if(length == 0) goto recv_frame_content;
			}
			else leave=1;
		break;
		case MINIMSG_STATE_RECV_FRAME_CONTENT:
recv_frame_content:
			dbg("state => MINIMSG_STATE_RECV_FRAME_CONTENT\n");
			dbg("(%d + %d ), %d\n",r,fds->recv_current_frame_byte,fds->recv_frame->length);
			if(r  >= fds->recv_frame->length - fds->recv_current_frame_byte ){
				copy = fds->recv_frame->length - fds->recv_current_frame_byte  ;
				ringbuffer_pop_data(rb,fds->recv_frame->content+fds->recv_current_frame_byte,copy);
				msg_append_frame(fds->recv_msg,fds->recv_frame);
				fds->recv_number_of_frame--;
				if(fds->recv_number_of_frame == 0){
					/* a complete message is received */
					fds->recv_state = MINIMSG_STATE_RECV_NUMBER_OF_FRAME;
					qdata = malloc( sizeof( server_thread_data_t));
					if(!qdata){
						dbg("fail to alloc\n");			
					}
					else{
						qdata->msg = fds->recv_msg;
						fds->recv_msg = NULL;
						/* add because fd_state is put in queue */
						fd_state_add_reference(fds);
						qdata->fds = fds;
						
						pthread_spin_lock(&server->lock);
						queue_push( server->recv_q,(void*)qdata); 
						pthread_spin_unlock(&server->lock);
						u = 1;
						s = write(server->recv_efd, &u, sizeof(uint64_t));
						if (s != sizeof(uint64_t)) handle_error("write");
					}
				}
				else
					fds->recv_state = MINIMSG_STATE_RECV_FRAME_LENGTH;
			}
			else{
				ringbuffer_pop_data(rb,fds->recv_frame->content+fds->recv_current_frame_byte,r);
				fds->recv_current_frame_byte +=r;
				dbg("receive : %d\n",fds->recv_current_frame_byte);
			}
		break;
		default:
			printf("undefined state\n");
			goto fail;
		break;
	  } // switch
     }// inner while
	} // outer while
	if (result <= 0) {  /* disconnect */
		if(result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    		if(result == 0)dbg("disconnect\n");
		if(fds->recv_msg){
			msg_free(fds->recv_msg);
			fds->recv_msg = NULL;
		}
		free_network_rw_event(fds);
	    free_fd_state(fds);
    }
	return;	
fail:
/* state machine error */
	printf("state machine error\n");
	if(fds->recv_msg){
		msg_free(fds->recv_msg);
		fds->recv_msg = NULL;
	}
	free_network_rw_event(fds);
	free_fd_state(fds);
}
/*

void
do_read(evutil_socket_t fd, short events, void *arg)
{
    fd_state_t *state = (fd_state_t*)arg;
    char buf[MINIMSG_MSGSERVER_BUFFER_SIZE];
    int i;
    ssize_t result;
  //  printf("start reading ...\n");
    while (1) {
        assert(state->write_event);
        result = recv(fd, buf, sizeof(buf), 0);
        if (result <= 0)
            break;

		state->recv_byte+=result;
		switch(state->recv_state){
			
		}
		if(state->recv_byte >= 4){
			
		}
        for (i=0; i < result; ++i)  {
	//	printf("char [ %c ]\n",buf[i]);
            if (state->buffer_used < sizeof(state->buffer))
                state->buffer[state->buffer_used++] = rot13_char(buf[i]);
            if (buf[i] == '\n') {
	//	printf("meet newline\n");
                assert(state->write_event);
		state->write_upto = state->buffer_used;
                event_add(state->write_event, NULL);
               
            }
        }
    }
//    printf("read %d [ %d ]\n",state->buffer_used,result);
    if (result == 0) {
        free_fd_state(state);
    } else if (result < 0) {
        if (errno == EAGAIN) // XXXX use evutil macro
            return;
        perror("recv");
        free_fd_state(state);
    }
}
*/
static void free_network_rw_event(fd_state_t *state)
{
	
    	if(state->read_event){
		event_free(state->read_event);
		state->read_event = NULL;
	}

        if(state->write_event){
		event_free(state->write_event);
		state->write_event = NULL;
        }
	state->send_state = state->recv_state = -1;
}

/* fd_state_t may be passed to other threads and passed back
 * but, when the connection is closed, the thread in charge of network I/O would
 * free it, but it should only free it when fd_state_t is not referenced by other threads
 * no lock is needed here because only one thread deals with its memory.
 */
static void free_fd_state(fd_state_t *state)
{
    state->refcnt--;
    msg_t* m;
	
    if(state->refcnt == 0){
    dbg("\n");
	free_network_rw_event(state);


	if(state->send_msg){
                msg_free(state->send_msg);
                state->send_msg = NULL;
        }
	if(state->send_frame){
		frame_free(state->send_frame);
		state->send_frame = NULL;
	}
    while(  m = queue_pop(state->send_q) ){
        msg_free(m);
    }

	close(state->sock);
    ringbuffer_destroy(state->rb_recv);
    ringbuffer_destroy(state->rb_send);
    queue_free(state->send_q);
    pthread_spin_destroy(&state->lock);
    free(state);
    
    }
}

static fd_state_t * alloc_fd_state(minimsg_socket_t* ss, evutil_socket_t fd)
{
	minimsg_context_t* ctx = ss->ctx;
    struct event_base* base = ctx->base;
    fd_state_t *state = malloc(sizeof(fd_state_t));
    list_node_t* list_node;
    if (!state)
        return NULL;

	if(pthread_spin_init(&state->lock,0)!=0) handle_error("pthread spin_init fails");

   
    state->read_event = event_new(base, fd, EV_READ|EV_PERSIST, msg_recv_nb, state);
    if (!state->read_event) {
        free(state);
        
        return NULL;
    }
    state->write_event = 
        event_new(base, fd, EV_WRITE, msg_send_nb, state);

    if (!state->write_event) {
        event_free(state->read_event);
        free(state);
        return NULL;
    }

    state->sock = fd;
    state->refcnt = 1;
    state->minimsg_socket = ss;
    
	state->rb_recv = ringbuffer_alloc(MINIMSG_MSGSERVER_BUFFER_SIZE);
	state->rb_send = ringbuffer_alloc(MINIMSG_MSGSERVER_BUFFER_SIZE);
	
	state->recv_state = MINIMSG_STATE_RECV_NUMBER_OF_FRAME;
	state->recv_frame = NULL;
	state->recv_number_of_frame = 0;
	state->recv_current_frame_byte = 0;
	state->recv_msg = NULL;
	
	state->send_state = 0;
	state->send_frame = NULL;
	state->send_ptr = NULL;
	state->send_byte = 0;
	state->send_msg = NULL;
	state->send_q = queue_alloc();
	if(!state->send_q){
		event_free(state->read_event);
		event_free(state->write_event);
		free(state);
		return NULL;
	}
	list_node = list_node_new((void*)state);
	if(!list_node){
		queue_free(state->send_q);
		event_free(state->read_event);
		event_free(state->write_event);
		free(state);
		return NULL;
	}
	state->list_node = list_node;
	

	
    return state;
}

void do_accept(evutil_socket_t listener, short event, void *arg)
{
	minimsg_socket_t* server = (minimsg_socket_t*)arg;
	minimsg_context_t* ctx = server->ctx;
    struct event_base *base = ctx->base;
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    fd_state_t *state;
    int one=1;
    int fd = accept(listener, (struct sockaddr*)&ss, &slen);
    if (fd < 0) { // XXXX eagain??
        perror("accept");
    } else if (fd > FD_SETSIZE) {
        close(fd); // XXX replace all closes with EVUTIL_CLOSESOCKET */
    } else {
		printf("a new connection\n");
	dbg("a new connection\n");
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        evutil_make_socket_nonblocking(fd);
        state = alloc_fd_state(server, fd);
        if(state){
			assert(state); /*XXX err*/
			assert(state->write_event);
			add_to_connected_list(ctx,state);
			event_add(state->read_event, NULL);
		}
		else{
			handle_error("alloc_fd_state fails");
		}
    }
}
/* This function is called when thread sends result back */
static void msg_server_read_result_from_thread_pool(evutil_socket_t fd, short events, void *arg)
{
	dbg("\n");
	msg_server_t *m ;
	/* read from result queue and send to network */
	ssize_t s;
	uint64_t u;
	int i;
	fd_state_t* fds;
	server_thread_data_t* qdata;
	m = (msg_server_t*) arg;
 
	s = read(fd, &u, sizeof(uint64_t));
        if (s != sizeof(uint64_t)){
               perror("read");
		return;
        }
        for(i = 0;  i< u ; i++){
		pthread_spin_lock(&m->thp->qlock);
                	qdata = (server_thread_data_t*) queue_pop(m->thp->q);
             	pthread_spin_unlock(&m->thp->qlock);
		fds = qdata->fds;
		free_fd_state(fds);
		if(fds->send_state != -1){
			dbg("message to send\n");
		//	msg_print(qdata->msg);
                	queue_push(fds->send_q,qdata->msg);
               		event_add(fds->write_event, NULL);
        	}
		else{
			dbg("%s: send is closed\n",__func__);
			msg_free(qdata->msg);
		}
		free(qdata);
        }	
}
static void* msg_server_thread_task_wrapper(void* arg)
{
	server_thread_data_t * d = (server_thread_data_t*)arg;
/*
	void* r;
	
	if(d->server->cb == NULL){
		fprintf(stderr,"callback null\n");
		return NULL;	
	}
	r = d->server->cb(d->msg);
	
	if(!r){
		free_fd_state(d->fds);
		free(d);
		return NULL;
	}
	d->msg = r;
	*/
	return d;		
}

msg_server_t* create_msg_server(void* base,int port)
{
/*	msg_server_t* server;
	int i;
	struct event *listener_event;
	struct event *thread_event;
	struct event *sigusr1_event;
	struct sockaddr_in sin;
	evutil_socket_t listener;
    listener = socket(AF_INET, SOCK_STREAM, 0);
    evutil_make_socket_nonblocking(listener);
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(port);

    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    if (bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return NULL;
    }

    if (listen(listener, 16)<0) {
        perror("listen");
        return NULL;
    }
    server = malloc( sizeof( msg_server_t ));
    if(!server)return NULL;
    
    server->cb = cb; 
    server->sock = listener;
    server->thp = thread_pool_alloc(threads );
    server->base = base;
    listener_event = event_new(base, listener, EV_READ|EV_PERSIST, do_accept, (void*)server );
    thread_event = event_new(base, server->thp->efd, EV_READ|EV_PERSIST, msg_server_read_result_from_thread_pool, (void*)server );
    sigusr1_event = evsignal_new(base,SIGUSR1,sigusr1_func,(void*)server);
	*/
    /*XXX check it */
	/*
    event_add(sigusr1_event,NULL);
    event_add(listener_event, NULL);
    event_add(thread_event, NULL);
    server->thread_event = thread_event;
    server->listener_event = listener_event;
    server->sigusr1_event = sigusr1_event;
    server->task_scheduler = task_scheduler;
    return server;
	*/
	return NULL;
}
void free_msg_server(msg_server_t* server)
{
	event_free(server->listener_event);
	close(server->sock);
	thread_pool_destroy(server->thp);
	event_free(server->thread_event);
	event_free(server->sigusr1_event);
	free(server);
}
static void sigusr1_func(evutil_socket_t fd, short event, void *arg)
{
	msg_server_t* server = (msg_server_t*)arg;
	printf("%s: got signal\n",__func__);	
	//event_base_dump_events(server->base,stdout);
    	free_msg_server(server);
}

msg_client_t* create_msg_clients()
{
	/*msg_client_t* c;
	c = (msg_client_t*) malloc( sizeof( msg_client_t));
	if(c){
		c->info = NULL;
	}
	return c;*/
	return NULL;
}
/*
int add_msg_clients(msg_client_t* c,int type,const char* location, int port,int id)
{
	client_info_t* info;
	info = (client_info_t*) malloc( sizeof( client_info_t));
	if(!info)return MINIMSG_FAIL;
	info->type = type;
	info->location = strdup(location);
	info->port = port;
	info->send_q = queue_alloc();
	info->recv_q = queue_alloc();
	info->send_efd = eventfd(0,0);
	info->id = id;
	info->next = NULL;
	if( info->location == NULL || info->send_q == NULL || info->recv_q == NULL || info->send_efd == -1){
		if(info->location) free(info->location);
		if(info->send_q) queue_free(info->send_q);
		if(info->recv_q) queue_free(info->recv_q);
		if(info->send_efd != -1) close(info->send_efd);
		return MINIMSG_FAIL;
	}

	if(c->info == NULL){
		c->info = info;
	}
	else{
		info->next =  c->info;
		c->info  = info;		
	}
	
	return MINIMSG_OK;
	
}*/
int connect_msg_clients(msg_client_t* c)
{/*
        struct sockaddr_in server;
	client_info_t* info;
	for(info = c->info ; info ; info = info->next){
        	info->sock = socket(AF_INET,SOCK_STREAM,0);
        	if(info->sock == -1)
        	{
			handle_error("fail to create socket\n");
        	}
		dbg("socket created\n"); 
		if(info->type == MINIMSG_AF_INET){
        		server.sin_addr.s_addr = inet_addr(info->location);
        		server.sin_family = AF_INET;
        		server.sin_port = htons(info->port);
		}
		else if(info->type == MINIMSG_AF_UNIX){
			//TODO
		}
		else handle_error("unknown type");

        	if (connect(info->sock,(struct sockaddr*)&server,sizeof(server))<0)
        	{
			if(info->type == MINIMSG_AF_INET)
				fprintf(stderr,"fail to connect to %s:%d\n",info->location,info->port);
        	        perror("connect failed.");
        	  
        	}	
	}*/
	return 0;
}
static void* do_io_task(void* arg)
{
	minimsg_context_t* ctx = (minimsg_context_t*) arg;

 	event_base_dispatch(ctx->base);

    	event_base_free(ctx->base);
}
static void timeout_cb(int fd, short event, void *arg)
{
	minimsg_context_t* ctx = (minimsg_context_t* ) arg;
	minimsg_socket_t* s;
	list_node_t* it;
	fd_state_t* state;
	int one = 1;
	dbg("timeout\n");
	while(1){
		pthread_spin_lock(&ctx->lock);
			it = list_lpop(ctx->connecting_list);
		pthread_spin_unlock(&ctx->lock);
		if(it == NULL) break;
		s = (minimsg_socket_t*)it->val;	
		if(minimsg_connect(s,s->server) == MINIMSG_FAIL){
			dbg("connect still fails\n");
		}
		else{
			dbg("connect succeeds not in first time\n");
		}
	}
    printf("leave timeout\n");
        
}
minimsg_context_t* minimsg_create_context()
{
	int s;
	minimsg_context_t * ctx = NULL;
	struct event* timer_event = NULL;
	struct event* sending_event= NULL;
	void* base = NULL;
	queue_t * send_q = NULL;
	int send_efd = -1;
	struct timeval tv = {TIMEOUT_SECONDS,0};
	list_t* connecting_list = NULL,*connected_list = NULL;
	ctx = (minimsg_context_t*) malloc( sizeof( minimsg_context_t) );
	
	if(!ctx)goto error;
	base = event_base_new();
	if(!base) goto error;
	send_efd = eventfd(0,0);
	if(send_efd == -1)goto error;
	sending_event=event_new(base, send_efd, EV_READ|EV_PERSIST, sending_handler, (void*)ctx );
	if(!sending_event) goto error;
	
	timer_event = event_new(base,-1, EV_TIMEOUT |EV_PERSIST,timeout_cb,(void*)ctx);
	if(!timer_event) goto error;
   
    send_q = queue_alloc();
	if(!send_q) goto error;
	
	connecting_list = list_new();
	if(!connecting_list) goto error;
	connected_list = list_new();
	if(!connected_list) goto error;
	
	if(pthread_spin_init(&ctx->lock,0)!=0) goto error;
	s = pthread_create(&ctx->thread,NULL,do_io_task,(void*)ctx);
    if(s != 0){
		pthread_spin_destroy(&ctx->lock);
		goto error;
	}
	
	
	ctx->send_q = send_q;
	ctx->send_efd = send_efd;
	ctx->base = base;
	ctx->connecting_list = connecting_list;
	ctx->connected_list = connected_list;
	ctx->timeout_event = timer_event;
	ctx->sending_event = sending_event;
	event_add(timer_event,&tv);
	event_add(sending_event, NULL);
	return ctx;
error:
	if(ctx)free(ctx);
	if(connecting_list) list_destroy(connecting_list);
	if(connected_list) list_destroy(connected_list);
	if(timer_event) event_free(timer_event);
	if(sending_event) event_free(sending_event);
	if(send_efd!=-1) close(send_efd);
	if(send_q) queue_free(send_q);
	if(base) event_base_free(base);
	return NULL;
}
minimsg_socket_t* minimsg_create_socket(minimsg_context_t* ctx,int type)
{
	minimsg_socket_t* s;

	s = (minimsg_socket_t*) malloc( sizeof( minimsg_socket_t));
	if(!s) return NULL;

	s->pending_send_q = queue_alloc();
	s->recv_q = queue_alloc();
	s->recv_efd = eventfd(0,0);
	
	s->listener = -1;
	s->type = type;
	s->next = NULL;
	s->ctx = ctx;
	s->isClient = 0;
	s->current = NULL;
	if(type == MINIMSG_REQ){
		s->state = MINIMSG_SOCKET_STATE_SEND;
	}
	else if(type == MINIMSG_REP){
		s->state = MINIMSG_SOCKET_STATE_RECV;
	}
	else{
		//TODO
		handle_error("not implemented\n");
	}
	if(pthread_spin_init(&s->lock,0)!=0) handle_error("pthread_spin_init");
	if(  s->recv_q == NULL || s->recv_efd  == -1  || s->pending_send_q == NULL){
		if(s->recv_q) queue_free(s->recv_q);
		if(s->recv_efd !=-1) close(s->recv_efd);
		if(s->pending_send_q) queue_free(s->pending_send_q);
		return NULL;
	}	
	return s;
}


int minimsg_connect(minimsg_socket_t* s, struct sockaddr_in server)
{	
	int sock;
	const char* str;
	fd_state_t* state;
	int one=1;
	
	s->isClient = 1;
	if(server.sin_family == AF_INET){
			sock = socket(AF_INET,SOCK_STREAM,0);
        	if(sock == -1)
        	{
        	        perror("could not create socket");
        	        return MINIMSG_FAIL;
        	}
        	puts("Socket created");
			s->server = server;
			//TODO, add it to event
        	
        	if (connect(sock,(struct sockaddr*)&s->server,sizeof(s->server))<0){
				/* add it to connecting pool */
					close(sock);
					add_to_connecting_list(s->ctx,s);
        	        perror("connect failed.");
        	        return MINIMSG_FAIL;
        	}
        	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
			evutil_make_socket_nonblocking(sock);
			state = alloc_fd_state(s, sock);
			s->current= state;
			state->refcnt++;
			add_to_connected_list(s->ctx,state);
			assert(state); /*XXX err*/
			assert(state->write_event);
			event_add(state->read_event, NULL);
	}
	else{
		//TODO
		handle_error("not implemented\n");
	}
	return MINIMSG_OK;
}
int minimsg_bind(minimsg_socket_t* s, unsigned port)
{
	minimsg_context_t* ctx = s->ctx;
	struct sockaddr_in sin;
	
	s->isClient = 0;
    s->listener = socket(AF_INET, SOCK_STREAM, 0);
    if(s->listener == -1){
       perror("could not create socket");
       return MINIMSG_FAIL;
    }
    
    evutil_make_socket_nonblocking(s->listener);
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(port);

    int one = 1;
    setsockopt(s->listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    if (bind(s->listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return MINIMSG_FAIL;
    }

    if (listen(s->listener, 16)<0) {
        perror("listen");
        return MINIMSG_FAIL;
    }
    s->listener_event = event_new(ctx->base, s->listener, EV_READ|EV_PERSIST, do_accept, (void*)s );
    event_add(s->listener_event,NULL);
    return MINIMSG_OK;
}
int minimsg_send(minimsg_socket_t* ss, msg_t* m)
{
	uint64_t uw;
	ssize_t s;
	minimsg_context_t* ctx;
	queue_data_t* q;
	
	ctx = ss->ctx;
	printf("state : %d\n",ss->state);
	if(check_send_state(ss) == MINIMSG_FAIL){
		handle_error("fail to pass check_send_state\n");
	}
	
	q = (queue_data_t*) malloc( sizeof( queue_data_t) );
	if(!q) return MINIMSG_FAIL;
	
	fd_state_add_reference(ss->current);
	if(ss->isClient){
		q->fds = ss->current;
	}
	else{
		/* TODO, further improvement is needed when server
		 * wants to start sending data first 
		 */
		q->fds = ss->current;
	}
	
	q->msg = m;
	pthread_spin_lock(&ctx->lock);
		queue_push( ctx->send_q,q); 
	pthread_spin_unlock(&ctx->lock);

	uw = 1;
	/* write is thread safe */
    s = write(ctx->send_efd, &uw, sizeof(uint64_t));
    if (s != sizeof(uint64_t)) handle_error("write");
	
	update_socket_state(ss);
	return MINIMSG_OK;
}
msg_t* minimsg_recv(minimsg_socket_t* ss)
{
	msg_t* m;
	queue_data_t* qd;
	uint64_t ur;
    ssize_t s;
    if(check_recv_state(ss) == MINIMSG_FAIL){
			handle_error("fail to pass check_recv_state\n");
	}
	dbg("pass check_recv_state\n");
	dbg("waiting for reading ... \n");
	s = read(ss->recv_efd, &ur, sizeof(uint64_t));
    if (s != sizeof(uint64_t)){
		handle_error("read error\n");
	}
	dbg("got %lu message\n",ur);
	if(ur > 1){
		ur--;
		s = write(ss->recv_efd,&ur,sizeof(uint64_t));
		if (s != sizeof(uint64_t)) handle_error("write error\n");
	}
	pthread_spin_lock(&ss->lock);
		qd = (queue_data_t*) queue_pop( ss->recv_q); 
	pthread_spin_unlock(&ss->lock);
	ss->current = qd->fds;
	
	update_socket_state(ss);
	
	return qd->msg;
}
static int check_send_state(const minimsg_socket_t* s)
{
	if(s->type == MINIMSG_REP || s->type == MINIMSG_REQ){
		if(s->state == MINIMSG_SOCKET_STATE_SEND ) return MINIMSG_OK;
	}
	return MINIMSG_FAIL;
}

static int check_recv_state(const minimsg_socket_t* s)
{
	if(s->type == MINIMSG_REP || s->type == MINIMSG_REQ){
		if(s->state == MINIMSG_SOCKET_STATE_RECV ) return MINIMSG_OK;
	}
	return MINIMSG_FAIL;
}
static void update_socket_state(minimsg_socket_t* s)
{
	if(s->type == MINIMSG_REP || s->type == MINIMSG_REQ){
			if(s->state == MINIMSG_SOCKET_STATE_RECV)
				s->state = MINIMSG_SOCKET_STATE_SEND;
			else if(s->state == MINIMSG_SOCKET_STATE_SEND)
				s->state = MINIMSG_SOCKET_STATE_RECV;
	}
	else{
		//TODO
	}
	
}
/* This function is called when queue has data to send to network */
static void sending_handler(evutil_socket_t fd, short events, void *arg)
{
	dbg("\n");
	/* read from result queue and send to network */
	ssize_t s;
	uint64_t u;
	int i;
	fd_state_t* fds;
	queue_data_t* qdata;
	minimsg_context_t* ctx = (minimsg_context_t*) arg;
	minimsg_socket_t* ms;
	s = read(fd, &u, sizeof(uint64_t));
    if (s != sizeof(uint64_t)){
         perror("read");
		return;
    }
    for(i = 0;  i< u ; i++){
		pthread_spin_lock(&ctx->lock);
			qdata = (queue_data_t*) queue_pop(ctx->send_q);
        pthread_spin_unlock(&ctx->lock);
		fds = qdata->fds;
		free_fd_state(fds);
		if(fds->send_state != -1){
			dbg("message to send\n");
		//	msg_print(qdata->msg);
            queue_push(fds->send_q,qdata->msg);
            event_add(fds->write_event, NULL);
        }
		else{
			dbg("send is closed, free msg\n");
			msg_free(qdata->msg);
		}
		free(qdata);
    }	
}
static void add_to_connecting_list(minimsg_context_t* ctx, minimsg_socket_t* s)
{
	pthread_spin_lock(&ctx->lock);
		list_rpush(ctx->connecting_list,s->list_node);
	pthread_spin_unlock(&ctx->lock);
}
static void add_to_connected_list(minimsg_context_t* ctx, fd_state_t* fds)
{
	pthread_spin_lock(&ctx->lock);
		list_rpush(ctx->connected_list,fds->list_node);
	pthread_spin_unlock(&ctx->lock);
}
int minimsg_free_socket(minimsg_socket_t* s)
{
	
}
static void fd_state_add_reference(fd_state_t* state)
{
	pthread_spin_lock(&state->lock);
		state->refcnt++;
	pthread_spin_unlock(&state->lock);
}

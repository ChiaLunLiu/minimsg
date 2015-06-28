#include "minimsg.h"
#include "queue.h"
#include "ringbuffer.h"
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include<sys/un.h>
#include <fcntl.h>

#include <event2/event.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#define TIMEOUT_SECONDS 3
#define ONE_CONTROL_MESSAGE ( 1ULL << 31)

typedef struct _queue_data{
	msg_t* msg;
	fd_state_t* fds;
}queue_data_t;


static fd_state_t * alloc_fd_state(minimsg_socket_t* server, evutil_socket_t fd);
static int free_fd_state(fd_state_t *state);

static void free_network_rw_event(fd_state_t *state);
static void msg_recv_nb(evutil_socket_t fd, short events, void *arg);
static void msg_send_nb(evutil_socket_t fd, short events, void *arg);

static int check_recv_state(const minimsg_socket_t* s);
static int check_send_state(const minimsg_socket_t* s);

static void update_socket_state(minimsg_socket_t* s);
static void sending_handler(evutil_socket_t fd, short events, void *arg);

static void send_control_message(minimsg_context_t* ctx, msg_t* m);
/*
 * control message to stop the network I/O thread
 */
static void kill_thread(minimsg_context_t* ctx);

static void control_handler(evutil_socket_t fd, short events, void *arg);


static void _minimsg_free_socket(minimsg_socket_t* s);
static int _minimsg_connect(minimsg_socket_t* s);
static void data_handler(evutil_socket_t fd, short events, void *arg);
static void do_accept(evutil_socket_t listener, short event, void *arg);


static void* thread_handler(void* arg);
/*
 *  timeout_handler
 *  is a timeout handler that does auto-reconnect  
 */
static void timeout_handler(int fd, short event, void *arg);



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
					qdata = malloc( sizeof( queue_data_t));
					if(!qdata){
						dbg("fail to alloc\n");			
					}
					else{
						qdata->msg = fds->recv_msg;
						fds->recv_msg = NULL;
						if(server->isClient == 0 ){
							pthread_spin_lock(&server->lock);
							/* reference count for pushing into queue */
								fds->refcnt++; 
							pthread_spin_unlock(&server->lock);
						}
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
/* free_network_rw_event
 * free network event when the connection is down
 * if the socket is a client, send control msg to prevent from being blocked
 */ 
static void free_network_rw_event(fd_state_t *state)
{
	minimsg_socket_t* ss;
	uint64_t uw ;
	ssize_t s;
	dbg("\n");
	ss = state->minimsg_socket;
	
	if(ss->type != MINIMSG_SEND_ONLY && ss->isClient && state->send_state !=-1){
		uw = ONE_CONTROL_MESSAGE;
		/* write is thread safe */
		s = write(ss->recv_efd, &uw, sizeof(uint64_t));
		if (s != sizeof(uint64_t)) handle_error("write");
	}
    	if(state->read_event){
		dbg("free read_event\n");
		event_free(state->read_event);
		state->read_event = NULL;
	}

    	if(state->write_event){
		dbg("free write_event\n");
		event_free(state->write_event);
		state->write_event = NULL;
    	}
	dbg("end\n");
	state->send_state = state->recv_state = -1;
}

/* fd_state_t may be passed to other threads and passed back
 * but, when the connection is closed, the thread in charge of network I/O would
 * free it, but it should only free it when fd_state_t is not referenced by other threads
 * no lock is needed here because only one thread deals with its memory.
 */
static int free_fd_state(fd_state_t *state)
{
   
    msg_t* m;
    queue_data_t* qd;
    int refcnt;
    queue_t* q;
    queue_t* tmp;
    minimsg_socket_t* ss = state->minimsg_socket;
    pthread_spin_lock(&ss->lock);
	state->refcnt--;
	refcnt = state->refcnt;
    pthread_spin_unlock(&ss->lock);
 
   dbg("refcnt: %d\n",refcnt);
    
    if(refcnt == 0){
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
	list_remove(ss->fd_list,state->ln);
    	free(state);
    }
    return refcnt;
}

static fd_state_t * alloc_fd_state(minimsg_socket_t* ss, evutil_socket_t fd)
{
	minimsg_context_t* ctx = ss->ctx;
    struct event_base* base = ctx->base;
    fd_state_t *state = malloc(sizeof(fd_state_t));
    dbg("\n");
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
    state->minimsg_socket = ss;
    state->refcnt = 1; 
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
	state->ln = list_node_new(state);
	if(!state->ln) handle_error("fail to alloc list_node_new\n");

	list_rpush(ss->fd_list,state->ln);	
	
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
			event_add(state->read_event, NULL);
		}
		else{
			handle_error("alloc_fd_state fails");
		}
    }
}

/*
 *  thread_handler 
 *  thread's function that handles all libevent stuff
 */
static void* thread_handler(void* arg)
{
	minimsg_context_t* ctx = (minimsg_context_t*) arg;
	dbg("thread_handler starts\n");
 	event_base_dispatch(ctx->base);
	dbg("thread handler ends\n");
 	return NULL;
}
static void timeout_handler(int fd, short event, void *arg)
{
	minimsg_context_t* ctx = (minimsg_context_t* ) arg;
	minimsg_socket_t* ss;
	list_node_t* it;
	list_t* list,*tmp;
	
	dbg("timeout\n");
	list = list_new();
	if(!list){
		dbg("list_new() fails\n");
		return;
	}
	
	tmp = ctx->connecting_list;
	ctx->connecting_list = list;
	list = tmp;
	while(1){
		it = list_lpop(list);
		if(it == NULL) break;
		ss = (minimsg_socket_t*)it->val;	
		free(it);
		if(_minimsg_connect(ss) == MINIMSG_FAIL){
			dbg("connect still fails\n");
		}
		else{
			dbg(" connect succeeds not in first time\n");
		}
	}
	
	list_destroy(list);
    dbg("leave timeout\n");
        
}
/*
 *  minimsg_create_context
 *  create all the needed memory and a thread to handle network I/O
 */
minimsg_context_t* minimsg_create_context()
{
	int s;
	minimsg_context_t * ctx = NULL;
	struct event* timer_event = NULL;
	struct event* data_event = NULL, *control_event = NULL;
	void* base = NULL;
	queue_t * data_q = NULL,* control_q = NULL;
	int data_efd = -1, control_efd = -1;
	struct timeval tv = {TIMEOUT_SECONDS,0};
	list_t* connecting_list = NULL,*sk_list = NULL;

	ctx = (minimsg_context_t*) malloc( sizeof( minimsg_context_t) );
	
	if(!ctx)goto error;
	base = event_base_new();
	if(!base) goto error;
	data_efd = eventfd(0,0);
	control_efd = eventfd(0,0);
	if(data_efd == -1 || control_efd == -1) goto error;
	data_event=event_new(base, data_efd, EV_READ|EV_PERSIST, data_handler, (void*)ctx );
	control_event=event_new(base, control_efd, EV_READ|EV_PERSIST, control_handler, (void*)ctx );
	timer_event = event_new(base,-1, EV_TIMEOUT |EV_PERSIST,timeout_handler,(void*)ctx);
	if(!data_event || !control_event || !timer_event) goto error;
	   
    data_q = queue_alloc();
    control_q = queue_alloc();
	if(!data_q || !control_q) goto error;

	connecting_list = list_new();
	sk_list = list_new();
	
	if(!connecting_list || !sk_list) goto error;
	
	if(event_add(timer_event,&tv)== -1 || event_add(data_event, NULL) == -1 || 
	event_add(control_event, NULL) == -1 )goto error;

	ctx->base = base;
	ctx->data_q = data_q;
	ctx->data_efd = data_efd;
	ctx->control_q = control_q;
	ctx->control_efd = control_efd;
	ctx->connecting_list = connecting_list;
	ctx->sk_list = sk_list;
	ctx->timeout_event = timer_event;
	ctx->data_event = data_event;
	ctx->control_event = control_event;
	
	if(pthread_spin_init(&ctx->lock,0)!=0) goto error;
	s = pthread_create(&ctx->thread,NULL,thread_handler,(void*)ctx);
    if(s != 0){
		pthread_spin_destroy(&ctx->lock);
		goto error;
	}
	
	return ctx;
error:
	handle_error("memory allocation fails\n");
	return NULL;
}
/*
 *  minimsg_create_socket
 *  prepare the memory, and add itself to ctx->sk_list
 *  the sock is not created yet, it would be created in minimsg_bind or minimsg_connect
 */
minimsg_socket_t* minimsg_create_socket(minimsg_context_t* ctx,int type)
{
	minimsg_socket_t* s;
	s = (minimsg_socket_t*) malloc( sizeof( minimsg_socket_t));
	if(!s) return NULL;

	s->pending_send_q = queue_alloc();
	s->recv_q = queue_alloc();
	s->recv_efd = eventfd(0,0);
	s->control_q = queue_alloc();
	s->listener = -1;
	s->type = type;
	s->ctx = ctx;
	s->isClient = 0;
	s->conn_path = NULL;
	s->listener_event = NULL;
	s->connection_state = 0;
	s->current = NULL;
	if(type == MINIMSG_REQ)	s->state = MINIMSG_SOCKET_STATE_SEND;
	else if(type == MINIMSG_REP)s->state = MINIMSG_SOCKET_STATE_RECV;
	else if(type == MINIMSG_SEND_ONLY)s->state = MINIMSG_SOCKET_STATE_SEND;
	else if(type == MINIMSG_RECV_ONLY)s->state = MINIMSG_SOCKET_STATE_RECV;
	else{
		//TODO
		handle_error("not implemented\n");
	}
	s->ln = list_node_new(s);
	s->fd_list = list_new();
	if(pthread_spin_init(&s->lock,0)!=0) handle_error("pthread_spin_init");
	if(  s->recv_q == NULL || s->recv_efd  == -1  || s->pending_send_q == NULL || s->ln == NULL || 
	s->fd_list == NULL  || s->control_q == NULL){
		handle_error("some memory can't be allocated in minimsg_create_socket\n");
	}
	
	pthread_spin_lock(&ctx->lock);
		list_rpush(ctx->sk_list,s->ln);	
	pthread_spin_unlock(&ctx->lock);
	
	
	return s;
}

int minimsg_connect(minimsg_socket_t* s,const char* conn_path)
{
	minimsg_context_t* ctx;
	msg_t* m;
	dbg("addr = %p\n",s);
	if(s->type != MINIMSG_REQ && s->type != MINIMSG_SEND_ONLY) handle_error("socket's type can't be used to do connect\n"); 

	m = msg_alloc();
	if(!m) return MINIMSG_FAIL;
	if(! (s->conn_path = strdup(conn_path)) ) return MINIMSG_FAIL;
	s->isClient = 1;
	s->connection_state = 1;
	ctx = s->ctx;
	msg_append_string(m,"connect");
	msg_append_string_f(m,"%p",s);
	send_control_message(ctx,m);
	
	return MINIMSG_OK;
}

static int _minimsg_connect(minimsg_socket_t* ss)
{	
	int sock;
	const char* str;
	fd_state_t* state;
	int one=1;
	uint64_t uw;
	ssize_t s;
	queue_data_t* qd;
	msg_t* m;
	int num;
	minimsg_context_t* ctx = ss->ctx;
	struct sockaddr_in remote;
	struct sockaddr_un local;
	struct sockaddr*  addr;
	socklen_t sock_len;
	char* conn_path;
	list_node_t * ln;
	char* port;
	dbg("\n");
	conn_path = strdup(ss->conn_path);
	if(!conn_path){
		dbg("strdup fails\n");	
		return MINIMSG_FAIL;
	}
	/* parse conn_path */
	if(!strncmp(conn_path,"local://",8)){	
		addr = (struct sockaddr*)&local;
		memset(&local,0,sizeof(local));
        	local.sun_family = AF_LOCAL;
        	strncpy(local.sun_path,conn_path + 8,sizeof(local.sun_path));
        	local.sun_path[ sizeof(local.sun_path)-1]='\0';
        	sock_len = SUN_LEN(&local);	
		sock = socket(AF_LOCAL,SOCK_STREAM,0);
        	if(sock < 0)
        	{
			dbg("fail to create socket\n");
        	        return MINIMSG_FAIL;
        	}
	}
	else if(!strncmp(conn_path,"remote://",9)){
		addr = (struct sockaddr*)&remote;
		port = strrchr(conn_path,':');
		*port = '\0';
		port++;
        	remote.sin_addr.s_addr = inet_addr(conn_path+9);
    		remote.sin_family = AF_INET;
    		remote.sin_port = htons( atoi( port) );		

		sock_len = sizeof( remote);
		sock = socket(AF_INET,SOCK_STREAM,0);
        	if(sock == -1)
        	{
			dbg("fail to create socket\n");
        	        return MINIMSG_FAIL;
        	}
	}
	free(conn_path);
        dbg("Socket created");
        if (connect(sock,addr,sock_len)<0){
		/* add it to connecting pool */
		close(sock);
		ln = list_node_new(ss);
		if(!ln) handle_error("list_node_new fails\n");
		list_rpush(ctx->connecting_list,ln);	
		dbg("connect fails\n");
		return MINIMSG_FAIL;
        }
      	dbg("connect succeeds\n");
       	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	evutil_make_socket_nonblocking(sock);
	state = alloc_fd_state(ss, sock);
	ss->current= state;
	pthread_spin_lock(&ss->lock);
	ss->connection_state = 2;
	pthread_spin_unlock(&ss->lock);
	/* reference count for client  */
	state->refcnt++;
	assert(state); /*XXX err*/
	assert(state->write_event);
	event_add(state->read_event, NULL);
	
	/* add all msg in pending_q to data_q */
	dbg("connect succeeds not in first time\n");
	/* no one would operate pending_send_q at this moment  so no lock */
	num = queue_size(ss->pending_send_q);
	dbg("there are %d pending messages to be sent\n",num);
	while( m = queue_pop(ss->pending_send_q) ){	
		qd = (queue_data_t*) malloc( sizeof( queue_data_t) );
		if(!qd) handle_error("malloc fails\n");
		if( ss->isClient == 0) handle_error("socket is not client \n");
		if( ss->current == NULL) handle_error("ss->current is NULL\n");
		qd->fds = ss->current;
		qd->msg = m;
		pthread_spin_lock(&ctx->lock);
		queue_push( ctx->data_q,qd);
		pthread_spin_unlock(&ctx->lock);
	}
			
	if(num > 0){
		uw = num;
		/* write is thread safe */
		s = write(ctx->data_efd, &uw, sizeof(uint64_t));
		if (s != sizeof(uint64_t)) handle_error("write");
	}			
	return MINIMSG_OK;
}
int minimsg_bind(minimsg_socket_t* s,const char* conn_path)
{
	minimsg_context_t* ctx = s->ctx;
	struct sockaddr_in remote;
	struct sockaddr_un local;
	struct sockaddr* addr;
	socklen_t sock_len;
	char* port;
	int sock;
	char* path;
	/* check if socket type is able to bind */
	if(s->type != MINIMSG_REP && s->type != MINIMSG_RECV_ONLY) handle_error("socket's type can't be used to do bind\n"); 
	s->isClient = 0;
    
	path = strdup(conn_path);
	if(!path){
		dbg("strdup fails\n");
		return MINIMSG_FAIL;
	}
	/* parse conn_path */
    	if(!strncmp(path,"local://",8)){
                addr = (struct sockaddr*)&local;
                memset(&local,0,sizeof(local));
                local.sun_family = AF_LOCAL;
                strncpy(local.sun_path,path + 8,sizeof(local.sun_path));
                local.sun_path[ sizeof(local.sun_path)-1]='\0';
                sock_len = SUN_LEN(&local);
		sock = socket(AF_LOCAL,SOCK_STREAM,0);
                if(sock < 0)
                {
                        dbg("fail to create socket\n");
                        return MINIMSG_FAIL;
                }
		unlink(path + 8);
    	}
    	else if(!strncmp(path,"remote://",9)){
                addr = (struct sockaddr*)&remote;
                port = strrchr(path,':');
                *port = '\0';
                port++;
                remote.sin_addr.s_addr = inet_addr(path+9);
                remote.sin_family = AF_INET;
                remote.sin_port = htons( atoi( port) );

                sock_len = sizeof( remote);
		sock = socket(AF_INET,SOCK_STREAM,0);
                if(sock < 0)
                {
                        dbg("fail to create socket\n");
                        return MINIMSG_FAIL;
                }

    	}
	free(path);
	s->listener = sock;
    	evutil_make_socket_nonblocking(s->listener);
    	int one = 1;
    	setsockopt(s->listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    	if (bind(s->listener,addr, sock_len) < 0) {
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
	queue_data_t* qd;
	int fail = 0;
	ctx = ss->ctx;
	printf("state : %d\n",ss->state);
	if(check_send_state(ss) == MINIMSG_FAIL){
		handle_error("fail to pass check_send_state\n");
	}
	/* slight optimization to avoid lock when connection state is 2, it is always 2 */
	if(ss->isClient == 0 ||  ss->connection_state == 2)	goto job;
	else if ( ss->connection_state == 0) handle_error("haven't called minimsg_connect yet\n");
	pthread_spin_lock(&ss->lock);
	if( ss->connection_state == 1){
		queue_push(ss->pending_send_q,m);
		pthread_spin_unlock(&ss->lock);
		goto end;
	}
	pthread_spin_unlock(&ss->lock);
job:
	/* network is disconnected */
	if(ss->current->send_state == -1){
		dbg("network is disconnected\n");
		msg_free(m);
		fail = 1;
		goto end;
	} 
	/* for server 
	 * drop reference due to ss->current and add reference due to adding into queue
	 *  both offsets, so do nothing
	 */ 
	
	qd = (queue_data_t*) malloc( sizeof( queue_data_t) );
	if(!qd) handle_error("malloc fails\n");
	
	
	qd->fds = ss->current;
	qd->msg = m;
	
	if(ss->isClient == 0){
		/* add to queue */
	pthread_spin_lock(&ss->lock);
		ss->current->refcnt++;
	pthread_spin_unlock(&ss->lock);
	}
	
	pthread_spin_lock(&ctx->lock);
		queue_push( ctx->data_q,qd);
	pthread_spin_unlock(&ctx->lock);
	/* free ss->current reference */
	if(ss->isClient == 0)free_fd_state(ss->current);
	
	uw = 1;
	/* write is thread safe */
	s = write(ctx->data_efd, &uw, sizeof(uint64_t));
	if (s != sizeof(uint64_t)) handle_error("write");
end:
	update_socket_state(ss);
	return  fail? MINIMSG_FAIL : MINIMSG_OK;
}
/*
 * minimsg_recv
 * 1) read control msg first and process it
 * 2) read msg data from ss->recv_q
 */
msg_t* minimsg_recv(minimsg_socket_t* ss)
{
	msg_t* m;
	queue_data_t* qd;
	uint64_t ur;
    ssize_t s;
    const char * str;
    if(check_recv_state(ss) == MINIMSG_FAIL){
			handle_error("fail to pass check_recv_state\n");
	}
	
	dbg("pass check_recv_state\n");
	dbg("waiting for reading ... \n");
again:
	s = read(ss->recv_efd, &ur, sizeof(uint64_t));
    if (s != sizeof(uint64_t)){

		dbg("read error\n");
		if(s == -1 && errno == EINTR){
			dbg("signal caught while reading\n");
			goto again;
		}
		return NULL;
	}
	/* check if there is control message */
	/* no data message would follow control message */
	if(ss->isClient){
		if(ur >= ONE_CONTROL_MESSAGE){
			dbg("disconnect\n");
			return NULL;
		}
	}
	dbg("got %lu message\n",ur);
	if(ur > 1){
		ur--;
again2:
		s = write(ss->recv_efd,&ur,sizeof(uint64_t));
		if (s != sizeof(uint64_t)){
			if(s == -1 && errno == EINTR){
				dbg("signal caught while writing\n");
				goto again2;
			}
			dbg("write error\n");
			return NULL;
		}
	}
	
	pthread_spin_lock(&ss->lock);
		qd = (queue_data_t*) queue_pop( ss->recv_q);
		if(ss->type != MINIMSG_RECV_ONLY &&  ss->isClient == 0){
			ss->current = qd->fds;
			ss->current->refcnt++;
			dbg("add reference count\n");
		}
	pthread_spin_unlock(&ss->lock);
	/* leave queue */
	if(ss->isClient == 0){
		dbg("free fd_state\n");
		free_fd_state(qd->fds); 
	}
	update_socket_state(ss);
	m = qd->msg;
	free(qd);
	return m;
}
static int check_send_state(const minimsg_socket_t* s)
{
	if(s->type == MINIMSG_REP || s->type == MINIMSG_REQ){
		if(s->state == MINIMSG_SOCKET_STATE_SEND ) return MINIMSG_OK;
	}
	else if(s->type == MINIMSG_RECV_ONLY)return MINIMSG_FAIL;
	else if(s->type == MINIMSG_SEND_ONLY)return MINIMSG_OK;
	return MINIMSG_FAIL;
}

static int check_recv_state(const minimsg_socket_t* s)
{
	if(s->type == MINIMSG_REP || s->type == MINIMSG_REQ){
		if(s->state == MINIMSG_SOCKET_STATE_RECV ) return MINIMSG_OK;
	}
	else if(s->type == MINIMSG_SEND_ONLY)return MINIMSG_FAIL;
	else if(s->type == MINIMSG_RECV_ONLY)return MINIMSG_OK;
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
	else if( s->type ==MINIMSG_SEND_ONLY){
		/* NOTHING TO DO */
	}
	else if( s->type ==MINIMSG_RECV_ONLY){
		/* NOTHING TO DO */
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
	minimsg_socket_t* ss;
	s = read(fd, &u, sizeof(uint64_t));
    if (s != sizeof(uint64_t)){
         perror("read");
		return;
    }
    for(i = 0;  i< u ; i++){
		pthread_spin_lock(&ctx->lock);
			qdata = (queue_data_t*) queue_pop(ctx->data_q);
        pthread_spin_unlock(&ctx->lock);
		fds = qdata->fds;
		ss = fds->minimsg_socket;
		if(fds->send_state != -1){
			dbg("message to send\n");
		//	msg_print(qdata->msg);
            queue_push(fds->send_q,qdata->msg);
            event_add(fds->write_event, NULL);
        }
		else{
			if(ss->isClient == 0) free_fd_state(fds);
			dbg("send is closed, free msg\n");
			msg_free(qdata->msg);
		}
		free(qdata);
    }	
}


int minimsg_free_socket(minimsg_socket_t* s)
{
	msg_t* m;
	minimsg_context_t* ctx = s->ctx;
	dbg("addr = %p\n",s);
	m = msg_alloc();
	if(!m)return MINIMSG_FAIL;
	
	pthread_spin_lock(&ctx->lock);
		list_remove(ctx->sk_list,s->ln);
	pthread_spin_unlock(&ctx->lock);
	s->ln = NULL;
	
	msg_append_string(m,"free socket");
	msg_append_string_f(m,"%p",s);
	send_control_message(ctx, m);
	
	return MINIMSG_OK;
}
static void kill_thread(minimsg_context_t* ctx)
{
	msg_t* m;
	dbg("\n");
	m = msg_alloc();
	if(!m) handle_error("msg_alloc fails\n");
	msg_append_string(m,"kill");
	send_control_message(ctx, m);
	
}
static void _minimsg_free_socket(minimsg_socket_t* s)
{	
	msg_t* m;
	queue_data_t* qd;
	fd_state_t* state;
	minimsg_context_t* ctx;
	minimsg_socket_t* ss,*tmp;
	list_node_t *ln,*prev = NULL;
	list_iterator_t* it;
	dbg("\n");
	ctx = s->ctx;
	/* free s->current before remove from fd_list 
 	 * because "remove from fd_list" would destroy all element from list
	 */
	if(s->isClient){
		if(s->current){
			dbg("socket is client so free_fd_state()\n");
			free_fd_state(s->current);
			s->current = NULL;
		}
	}

	/* remove from fd_list */
	dbg("remove all fd in fd_list\n");
	it = list_iterator_new(s->fd_list, LIST_HEAD);
	prev = list_iterator_next(it);
	while(prev){
		ln = list_iterator_next(it);
		dbg("free fd state: %p\n",(void*)prev->val);
		/* no need to free list_node_t because free_fd_state does it */
		free_fd_state( (fd_state_t*) prev->val);
		prev = ln;
	}	
	list_iterator_destroy(it);
	
	dbg("destroy fd_list\n");
	list_destroy(s->fd_list);
		
	/* remove from connecting list */
	dbg("try to remove socket from connecting list if it is there\n");
	it = list_iterator_new(ctx->connecting_list, LIST_HEAD);
	while( ln = list_iterator_next(it)){
		dbg("element in connecting list\n");
		ss = (minimsg_socket_t*) ln->val;
		if( ss == s){
			dbg("remove minimsg socket from connecting list\n");
			list_remove(ctx->connecting_list,ln);
			break;
		}
	}
	list_iterator_destroy(it);
	
	dbg("free msg in pending_send_q\n");
		while( m = queue_pop(s->pending_send_q)){
			msg_free(m);
		}
	dbg("free pending_send_q\n");
	queue_free(s->pending_send_q);
	
	
	while(qd = queue_pop(s->recv_q)){
		msg_free(qd->msg);
		free_fd_state(qd->fds);
		free(qd);
	}
	queue_free(s->recv_q);
	
	
	dbg("close recv_efd\n");
	close(s->recv_efd);
	pthread_spin_destroy(&s->lock);
	/* free_fd_state would send msg to s->control_q
	 * so free control_q later than free_fd_state */
	while( m =queue_pop(s->control_q)){
		msg_free(m);
	}
	queue_free(s->control_q);

	if(s->listener_event){
		dbg("event_free listener event\n");
		event_free(s->listener_event);
	}
	if(s->conn_path) free(s->conn_path);
	if(s->listener > -1) close(s->listener);
	free(s);

}
/*
 *  minimsg_free_context
 *  send a kill message to network I/O thread to close all network connection 
 *  and terminate itself.
 *  pthread_join to wait for network I/O thread to complete the termination 
 *  free context memory and also minimsg sockets
 */
int minimsg_free_context(minimsg_context_t* ctx)
{
	fd_state_t* state;
	minimsg_socket_t* sk;
	list_node_t* ln;
	queue_data_t* qd;
	msg_t* m;
	dbg("\n");
	kill_thread(ctx);
	pthread_join(ctx->thread,NULL);
	dbg("free minimsg socket\n");
	while( ln = list_lpop(ctx->sk_list)){
		dbg("free sk\n");
		sk = (minimsg_socket_t*)ln->val;
		_minimsg_free_socket(sk);
		free(ln);
	}
	list_destroy(ctx->sk_list);
	dbg("free ctx->connecting list\n");
	while( ln = list_lpop(ctx->connecting_list)){
		free(ln);
	}
	list_destroy(ctx->connecting_list);
	
	event_free(ctx->timeout_event);
	
	pthread_spin_destroy(&ctx->lock);
	/* free data */
	event_free(ctx->data_event);
	close(ctx->data_efd);
	while( qd = queue_pop(ctx->data_q)){
		free_fd_state(qd->fds);
		msg_free(qd->msg);
		free(qd);
	}
	queue_free(ctx->data_q);
	
	/* free control */
	event_free(ctx->control_event);
	close(ctx->control_efd);
	while( m = queue_pop(ctx->control_q)){
		msg_free(m);
	}
	queue_free(ctx->control_q);
	event_base_free(ctx->base);
	free(ctx);
}
static void send_control_message(minimsg_context_t* ctx, msg_t* m)
{
	uint64_t s;
	ssize_t u;
	dbg("\n");
	pthread_spin_lock(&ctx->lock);
		queue_push(ctx->control_q,m); 
	pthread_spin_unlock(&ctx->lock);
	u = 1;
	s = write(ctx->control_efd, &u, sizeof(uint64_t));
	if (s != sizeof(uint64_t)) handle_error("write");
}
static void control_handler(evutil_socket_t fd, short events, void *arg)
{
	minimsg_context_t* ctx = (minimsg_context_t*) arg;
	ssize_t s;
	uint64_t u;
	msg_t* m;
	int i,j;
	int num;
	unsigned long ptr_addr;
	const char* str;
	const char* type;
	list_iterator_t *it;
	list_node_t* nd ;
	list_node_t* tmp;
	minimsg_socket_t* ss;
	dbg("\n");

	
	s = read(fd, &u, sizeof(uint64_t));
    if (s != sizeof(uint64_t)){
        perror("read");
		return;
    }
    for(i = 0;  i< u ; i++){
		pthread_spin_lock(&ctx->lock);
			m = (msg_t*) queue_pop(ctx->control_q);
		pthread_spin_unlock(&ctx->lock);
		num = msg_number_of_frame(m);
		type = msg_content_at_frame(m,0);
		/* free minimsg_socket */
		if(!strcmp(type,"free socket")){
			//TODO, atoi is not the right type 
			dbg("content = %s\n",msg_content_at_frame(m,1));
			ss =(minimsg_socket_t*) strtoul (msg_content_at_frame(m,1), NULL, 16);
			dbg("free minimsg socket %p\n",(void*)ss);
			_minimsg_free_socket(ss);
		}
		else if(!strcmp(type,"connect")){
			dbg("recv msg connect\n");
			ss = (minimsg_socket_t*)strtoul (msg_content_at_frame(m,1), NULL, 16);
			dbg("addr = %p\n",(void*)ss);
			_minimsg_connect(ss);
		}
		else if(!strcmp(type,"kill")){
			dbg("receive msg kill\n");
			if(event_base_loopbreak(ctx->base) == -1) handle_error("event_base_loopbreak fails\n");
		}
		msg_free(m);
	} /* end for */
}
/* This function is called when user sends result back */
static void data_handler(evutil_socket_t fd, short events, void *arg)
{
	minimsg_context_t* ctx;
	/* read from result queue and send to network */
	ssize_t s;
	uint64_t u;
	int i;
	int num=1;
	fd_state_t* fds;
	queue_data_t* qdata;
	minimsg_socket_t* ss;
	dbg("\n");
	ctx = (minimsg_context_t*) arg;
	
	s = read(fd, &u, sizeof(uint64_t));
        if (s != sizeof(uint64_t)){
               perror("read");
		return;
        }
    for(i = 0;  i< u ; i++){
		pthread_spin_lock(&ctx->lock);
			qdata = (queue_data_t*) queue_pop(ctx->data_q);
        pthread_spin_unlock(&ctx->lock);
		fds = qdata->fds;
		ss = fds->minimsg_socket;
		if(ss->isClient == 0) num = free_fd_state(fds);
		dbg("refcnt = %d\n",num);
		if(num > 0 && fds->send_state!=-1){
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

inline int minimsg_socket_recv_fd(const minimsg_socket_t* s)
{
	return s->recv_efd;
}

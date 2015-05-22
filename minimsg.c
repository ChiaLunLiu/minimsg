#include "minimsg.h"
#include "queue.h"
#include "ringbuffer.h"
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <errno.h>

/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
/* For fcntl */
#include <fcntl.h>

#include <event2/event.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static int (*g_callback)(msg_t* );

static fd_state_t * alloc_fd_state(struct event_base *base, evutil_socket_t fd);
static void msg_recv_nb(evutil_socket_t fd, short events, void *arg);
static void msg_send_nb(evutil_socket_t fd, short events, void *arg);




static int _frame_send(int sock,frame_t* f,int flags);

static void qmsg_free(void* f)
{
	msg_t* tmp = (msg_t*)f;
	msg_free(tmp);
}

int frame_transfer_byte(const frame_t* f)
{
	return f->length + 4;
}
frame_t* frame_string(const char* str)
{
	frame_t * f;
	int len = strlen(str);
	f = frame_alloc( len );
	if(!f) return NULL;
	memcpy(f->content,str,len);
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
inline const char*  frame_content(const frame_t* f)
{
	return f->content;
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
	char* ptr = (char*)&(f->length);
	int total = f->length + 4;
	
	while(sent_byte < total){
		
		r = send(sock,ptr+sent_byte,total - sent_byte,flags);
		if(r < 0)break;
		sent_byte+=r;
	}
	frame_free(f);
	if(r < 0)return FAIL;
	return OK;
}

int frame_recv(int sock,frame_t** f)
{
	int r = 0;
	int recv_byte = 0;
	int length = 0;
	char* ptr = (char*)&length;
	while(recv_byte < 4){
		r=recv(sock,ptr + recv_byte ,4 - recv_byte,0);
		if(r<=0)break;
		recv_byte+=r;
	};
	if(r <= 0)return FAIL;

	*f = frame_alloc( length+1);

	if(*f == NULL ) return FAIL;
	recv_byte = 0;
	while(recv_byte < length){
		r=recv(sock,(*f)->content + recv_byte ,length - recv_byte,0);
		if(r<0)break;
		recv_byte+=r;
	};
	if( r <= 0){
		frame_free(*f);
		return FAIL;
	}
	/* add '\0' in the end */
	(*f)->content[length ] ='\0';
	return OK;
}
int msg_append_string(msg_t* m,const char* str)
{
	frame_t * f;
	f = frame_string(str);
	if(!f)return FAIL;
	msg_append_frame(m,f);
	return OK;
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
}
msg_t* msg_alloc()
{
	msg_t* m;
	m = malloc( sizeof( msg_t));
	if(!m)return NULL;
	m->frames = 0;
	m->front = m->end = NULL;
	return m;
}
void msg_free(msg_t* m)
{
	frame_t* f, *tmp;
	for(f = m->front ; f ;){
		tmp = f;
		f = f->next;
		frame_free(tmp);
	}
	free(m);
}
int msg_send(int sock,msg_t* m)
{
	int r=0;
	int sent_byte = 0;
	frame_t * tmp;
	frame_t* f;
	char* ptr = (char*)&(m->frames);

	while(sent_byte < 4){
		r = send(sock,ptr+sent_byte,4 - sent_byte,MSG_MORE);
		if(r < 0 )break;
		sent_byte+=r;
	};
	if(r < 0){
		msg_free(m);
		return r;
	}
	for(f = m->front ; f ; ){
		tmp = f;
		f = f->next;
		if(f == NULL){
			if(frame_send(sock,tmp) != OK)goto fail;
		}
		else{
			if(frame_sendm(sock,tmp) != OK) goto fail;
		}
	}
	free(m);
	return OK;
fail:
	printf("fail to %s\n",__func__);
	while(f){
		tmp = f;
		f = f->next;
		frame_free(tmp);
	}
	free(m);
	return FAIL;
}
frame_t* msg_pop_frame(msg_t* m)
{
	frame_t* f;
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
frame_t* msg_front_frame(msg_t* m)
{
	frame_t* f;
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
	fprintf(stderr,"[debug]: start length ...");
	while(recv_byte < 4){
		r=recv(sock,ptr + recv_byte ,4 - recv_byte,0);
		printf("r:%d\n",r);
		if(r<=0)break;
		recv_byte+=r;
	};
	printf(" done\n");
	if(r <= 0)return FAIL;

	printf("number of frames: %d\n",frames);
	tmp = msg_alloc();
	if(!tmp)return FAIL;

	for(i = 0;i < frames ; i++){
		printf("[debug] : %d\n",i);
		if(frame_recv(sock,&f) != OK) goto fail;
		printf("append\n");
		msg_append_frame(tmp,f);
	}
	*m = tmp;
	return OK;
fail:
	printf("[debug]: fail\n");
	msg_free(tmp);
	return FAIL;
}
void msg_print(const msg_t * m)
{
	int cnt = 0;
	const char* reply;
	frame_t * tmp;
	
	for(tmp = m->front ; tmp ; tmp = tmp->next , cnt++){
		printf("[frame %d]: ",cnt);
		reply = frame_content(tmp);
		printf("%s\n",reply);
	}	
}
static void msg_send_nb(evutil_socket_t fd, short events, void *arg)
{
	int leave = 0;
	fd_state_t* fds = (fd_state_t*)arg;
	int r=0;
	int len;
	while(leave == 0){

	  switch(fds->send_state){
		case MINIMSG_STATE_SEND_INIT:
			fds->send_byte = 0;
			fds->send_ptr = (char*)&fds->send_msg->frames;
			fds->send_state = MINIMSG_STATE_SEND_NUMBER_OF_FRAME;

		break;
		case MINIMSG_STATE_SEND_NUMBER_OF_FRAME:
			r=send(fds->sock,fds->send_ptr+ fds->send_byte,4 - fds->send_byte,0);
			if(r <= 0){
				/* the other end close the connection */
				if(r == 0)  goto fail;

				if(errno == EINTR)continue;
				else if(errno == EAGAIN || errno ==  EWOULDBLOCK){
					leave = 1;
					continue;
				}
				else goto fail; 
			}

			if(r + fds->send_byte == 4){
				fds->send_frame = msg_pop_frame(fds->send_msg);
				fds->send_ptr = (char*)&fds->send_frame->length;
				fds->send_byte =0;
				fds->send_state = MINIMSG_STATE_SEND_FRAME_LENGTH;
			}
			else{
				fds->send_byte+=r;
				leave = 1;
			}
		break;
		case MINIMSG_STATE_SEND_FRAME_LENGTH:
			r=send(fds->sock,fds->send_ptr+ fds->send_byte,4 - fds->send_byte,0);
			if(r <= 0){
				/* the other end close the connection */
				if(r == 0)  goto fail;

				if(errno == EINTR)continue;
				else if(errno == EAGAIN || errno ==  EWOULDBLOCK){
					leave = 1;
					continue;
				}
				else goto fail; 
			}

			if(r + fds->send_byte == 4){
				fds->send_ptr = frame_content(fds->send_frame);
				fds->send_byte = 0;
				fds->send_state = MINIMSG_STATE_SEND_FRAME_CONTENT;
			}else{
				fds->send_byte+=r;
				leave =1;
			}	
		break;
		case MINIMSG_STATE_SEND_FRAME_CONTENT:
			len = ntohl(fds->send_frame->length);
			r=send(fds->sock,fds->send_ptr+ fds->send_byte,len - fds->send_byte,0);
			if(r <= 0){
				/* the other end close the connection */
				if(r == 0)  goto fail;

				if(errno == EINTR)continue;
				else if(errno == EAGAIN || errno ==  EWOULDBLOCK){
					leave = 1;
					continue;
				}
				else goto fail; 
			}

			if( r + fds->send_byte == len) {
				fds->send_frame = msg_pop_frame(fds->send_msg);

				/* end of frame */
				if(fds->send_frame == NULL){					
					fds->send_state = MINIMSG_STATE_SEND_INIT;
					msg_free(fds->send_msg);
					//TODO
					// callback ???
					return;
				}
				else{
					fds->send_ptr = frame_content(fds->send_frame);
					fds->send_byte = 0;		
					fds->send_state = MINIMSG_STATE_SEND_FRAME_LENGTH;
				}
			}
			else{
				fds->send_byte+=r;
				leave =1;
			}
		break;
		default:
			printf("undefined state\n");
			goto fail;
		break;
	  }
	}
	return;
	
fail:
	printf("fail\n");
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
	fd_state_t* fds = (fd_state_t*)arg;
	rb = fds->rb_recv;
	int leave = 0;

	
	while( copy = ringbuffer_linear_freesize(rb) ){
		dst = ringbuffer_get_end(rb);
		r=recv(fds->sock,dst,copy,0);
		if(r<=0)goto fail;
		/* raw manipulation of ring buffer in order not to do double copy */
		rb->end+=r;
		rb->free_space-=r;
		if(rb->end == rb->length)rb->end = 0;
	}
	
	while(leave == 0){
	  r = ringbuffer_datasize(rb);
	  if(r == 0)break;

	  switch(fds->recv_state){
		case MINIMSG_STATE_RECV_NUMBER_OF_FRAME:
			if(r >= 4){
				ringbuffer_pop_data(rb,(char*)&length,4);		
				length = ntohl(length);
				if(length > MINIMSG_MAX_NUMBER_OF_FRAME)goto fail;
				fds->recv_msg  = msg_alloc();
				if(!fds->recv_msg)goto fail;
				fds->recv_number_of_frame = length;
				fds->recv_state = MINIMSG_STATE_RECV_FRAME_LENGTH;
			}
			else leave =1;
		break;
		case MINIMSG_STATE_RECV_FRAME_LENGTH:
			if(r >= 4){
				ringbuffer_pop_data(rb,(char*)&length,4);		
				length = ntohl(length);
				if(length > MINIMSG_MAX_FRAME_CONTENT_SIZE)goto fail;
				fds->recv_frame = frame_alloc( length+ 1);
				if(!fds->recv_frame) goto fail;
				fds->recv_current_frame_byte = 0;
				fds->recv_state = MINIMSG_STATE_RECV_FRAME_CONTENT;
			}		
			else leave = 1;				
		break;
		case MINIMSG_STATE_RECV_FRAME_CONTENT:
			if(r + fds->recv_current_frame_byte >= fds->recv_frame->length){
				copy = r + fds->recv_current_frame_byte - fds->recv_frame->length;
				ringbuffer_pop_data(rb,fds->recv_frame->content+fds->recv_current_frame_byte,copy);
				msg_append_frame(fds->recv_msg,fds->recv_frame);
				fds->recv_number_of_frame--;
				if(fds->recv_number_of_frame == 0){
					fds->recv_state = MINIMSG_STATE_RECV_NUMBER_OF_FRAME;
					//TODO
					// execute callback
					//if( queue_push(q,fds->recv_msg)  == QUEUE_FAIL)goto fail;
				}
				else
					fds->recv_state = MINIMSG_STATE_RECV_FRAME_LENGTH;
			}
			else{
				ringbuffer_pop_data(rb,fds->recv_frame->content+fds->recv_current_frame_byte,r);
				fds->recv_current_frame_byte +=r;
			}
		break;
		default:
			printf("undefined state\n");
			goto fail;
		break;
	  }
	}
	return;	
fail:
	printf("fail\n");
	//TODO
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


static fd_state_t *
alloc_fd_state(struct event_base *base, evutil_socket_t fd)
{
    fd_state_t *state = malloc(sizeof(fd_state_t));
    if (!state)
        return NULL;
    state->read_event = event_new(base, fd, EV_READ|EV_PERSIST, msg_recv_nb, state);
    if (!state->read_event) {
        free(state);
        return NULL;
    }
    state->write_event =
        event_new(base, fd, EV_WRITE|EV_PERSIST, msg_send_nb, state);

    if (!state->write_event) {
        event_free(state->read_event);
        free(state);
        return NULL;
    }

    state->sock = fd;
    assert(state->write_event);
    
	state->rb_recv = ringbuffer_alloc(MINIMSG_MSGSERVER_BUFFER_SIZE);
	state->rb_send = ringbuffer_alloc(MINIMSG_MSGSERVER_BUFFER_SIZE);
	
	state->recv_state = MINIMSG_STATE_RECV_NUMBER_OF_FRAME;
	state->recv_frame = NULL;
	state->recv_number_of_frame = 0;
	state->recv_current_frame_byte = 0;
	state->recv_msg = NULL;
	
	state->send_state = MINIMSG_STATE_SEND_NUMBER_OF_FRAME;
	state->send_frame = NULL;
	state->send_ptr = NULL;
	state->send_byte = 0;
	state->send_msg = NULL;
	
    return state;
}

static void do_accept(evutil_socket_t listener, short event, void *arg)
{
    struct event_base *base = arg;
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    fd_state_t *state;
    int fd = accept(listener, (struct sockaddr*)&ss, &slen);
    if (fd < 0) { // XXXX eagain??
        perror("accept");
    } else if (fd > FD_SETSIZE) {
        close(fd); // XXX replace all closes with EVUTIL_CLOSESOCKET */
    } else {
//	printf("accept a new fd\n");
        evutil_make_socket_nonblocking(fd);
        state = alloc_fd_state(base, fd);
        assert(state); /*XXX err*/
        assert(state->write_event);
        event_add(state->read_event, NULL);
    }
}


void runMsgServer(unsigned port,int (*callback)(msg_t* ))
{
	evutil_socket_t listener;
    struct sockaddr_in sin;
    struct event_base *base;
    struct event *listener_event;

    base = event_base_new();
    if (!base)
        return; /*XXXerr*/
    g_callback = callback;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(port);

    listener = socket(AF_INET, SOCK_STREAM, 0);
    evutil_make_socket_nonblocking(listener);

    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    if (bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return;
    }

    if (listen(listener, 16)<0) {
        perror("listen");
        return;
    }

    listener_event = event_new(base, listener, EV_READ|EV_PERSIST, do_accept, (void*)base);
    /*XXX check it */
    event_add(listener_event, NULL);

    event_base_dispatch(base);
}

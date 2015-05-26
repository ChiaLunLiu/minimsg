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
//#define DEBUG 1

#if defined(DEBUG)
 #define dbg(fmt, args...) do{ fprintf(stderr, "%s(%d)/%s: " fmt, \
    __FILE__, __LINE__, __func__, ##args); }while(0)
#else
 #define dbg(fmt, args...) do{}while(0)/* Don't do anything in release builds */
#endif




static fd_state_t * alloc_fd_state(struct event_base *base, evutil_socket_t fd);
static void free_fd_state(fd_state_t *state);
static void msg_recv_nb(evutil_socket_t fd, short events, void *arg);
static void msg_send_nb(evutil_socket_t fd, short events, void *arg);
static void free_fd_state_send(fd_state_t * fds);



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
int msg_append_string(msg_t* m,const char* str)
{
	frame_t * f;
	f = frame_string(str);
	if(!f)return MINIMSG_FAIL;
	msg_append_frame(m,f);
	return MINIMSG_OK;
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
	dbg("[debug]: start length ...");
	while(recv_byte < 4){
		r=recv(sock,ptr + recv_byte ,4 - recv_byte,0);
//		printf("r:%d\n",r);
		
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
void msg_print(const msg_t * m)
{
	int cnt = 0;
	int i;
	const char* reply;
	frame_t * tmp;
	dbg("%s\n",__func__);
	for(tmp = m->front ; tmp ; tmp = tmp->next , cnt++){
		printf("[frame %d]\n",cnt);
		reply = frame_content(tmp);
		printf("(%d):", tmp->length);
		for(i=0;i<tmp->length;i++){
			printf("%c",reply[i]);
		}
		printf("\n");
//		printf("%s\n",reply);
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
	dbg("");
	dbg("state : %d\n",fds->send_state);

	if(fds->send_state == 0)goto stage0;
	if(fds->send_state == 1) goto stage1;
	else if(fds->send_state == 2)goto stage2;
	else if(fds->send_state == 3)goto stage3;
	else {
		dbg("[bug]: unknown stage\n");
		free_fd_state_send(fds);
		return;
	}	
stage0:
	dbg("state0\n");
	
	fds->send_msg  = queue_pop(fds->send_q);
	if(fds->send_msg == NULL){
		dbg("nothing to send\n");
		return;
	}
	fds->send_buf = htonl(fds->send_msg->frames);
	fds->send_frame = msg_pop_frame(fds->send_msg);
	if(fds->send_frame == NULL){
		dbg("[bug]: send_frame\n");
		free_fd_state_send(fds);
		return;
	}
	fds->send_ptr = (char*)&(fds->send_buf);
	fds->send_byte = 0;
	
stage1:
	dbg("state1\n");
	while(fds->send_byte < 4){
		r = send(sock,fds->send_ptr+fds->send_byte,4 - fds->send_byte,0);
		if(r <= 0 ){
				if(r == 0){
					if(errno == EPIPE){
						/* the other end closes receiving
						 * flush all sending data */
						free_fd_state_send(fds);
						return;
					}

				}
				if(errno == EINTR)continue;
				else if(errno == EAGAIN || errno ==  EWOULDBLOCK){
					fds->send_state = 1;
					event_add(fds->write_event, NULL);
					dbg("sending is blocked, add send event\n");
					return;
				}	
				else{
					/* other error 
					 * flush all sending data */
					perror("other error");
					free_fd_state_send(fds);
					return;
				}				
		}
		fds->send_byte+=r;
	};

stage2:
	dbg("state15\n");
	fds->send_state = 3;
	fds->send_content_byte = fds->send_frame->length + 4;
	fds->send_frame->length = htonl(fds->send_frame->length);
	fds->send_byte = 0;
	fds->send_ptr = (char*)&(fds->send_frame->length);
	dbg("frame content size : %d,%d\n",fds->send_content_byte,fds->send_frame->length);
stage3:
	dbg("state2\n");
	while(fds->send_byte < fds->send_content_byte){	
		r = send(sock,fds->send_ptr+fds->send_byte,fds->send_content_byte - fds->send_byte,0);
		if(r <= 0 ){
				if(r == 0){
					if(errno == EPIPE){
						/* the other end closes receiving
						 * flush all sending data */
						free_fd_state_send(fds);
						return;
					}

				}
				if(errno == EINTR)continue;
				else if(errno == EAGAIN || errno ==  EWOULDBLOCK){
					fds->send_state = 3;
					event_add(fds->write_event, NULL);
					dbg("sending is blocked, add send event\n");
					return;
				}	
				else{
					/* other error 
					 * flush all sending data */
					perror("other error");
					free_fd_state_send(fds);
					dbg("other error\n");
					return;
				}				
		}
		fds->send_byte+=r;
	}
	fds->send_frame = msg_pop_frame(fds->send_msg);
	if(fds->send_frame == NULL){	
		fds->send_state = 0;
		goto stage0;
	}
	else goto stage2;
	
	
	return;
	
}
static void msg_recv_nb(evutil_socket_t fd, short events, void *arg)
{
	static int debug = 0;
	int r;
	int recv_space;
	unsigned int length;
	char* ptr;	
	ringbuffer_t * rb ;
	int copy;
	char* dst;
	fd_state_t* fds = (fd_state_t*)arg;
	rb = fds->rb_recv;
	int leave ;
	int result;
	char buf[256];
	dbg("%s\n",__func__);
	
	
	while(1){
		
		copy = ringbuffer_freesize(rb);
		if(copy > 256) copy = 256;
		result=recv(fds->sock,buf,copy,0);
		if(result<=0){
			if(result <0 && errno == EINTR)continue;
			perror("recv");
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
			}
			else leave=1;
		break;
		case MINIMSG_STATE_RECV_FRAME_CONTENT:
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
					if(fds->send_state != -1){
						queue_push(fds->send_q,fds->recv_msg);
						event_add(fds->write_event, NULL);
					}
					else{
						/* sending is closed */
						dbg("send is closed\n");
						msg_free(fds->recv_msg);
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
	if (result == 0) {  /* disconnect */
        	free_fd_state(fds);
    	}else if (result < 0) {  /* error */
        	if (errno == EAGAIN) // XXXX use evutil macro
        	    return;
        	perror("recv");
        	free_fd_state(fds);
    	}
	return;	
fail:
	printf("fail\n");
	free_fd_state(fds);
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
static void free_fd_state(fd_state_t *state)
{
   dbg("\n");
    event_free(state->read_event);
    event_free(state->write_event);
    close(state->sock);
    ringbuffer_destroy(state->rb_recv);
    ringbuffer_destroy(state->rb_send);
    queue_free(state->send_q);
    free(state);
}

static void free_fd_state_send(fd_state_t * fds)
{
	fds->send_state = -1;
	if(fds->send_msg){
		msg_free(fds->send_msg);
		fds->send_msg = NULL;
	}
	shutdown(fds->sock,SHUT_WR);

}
static fd_state_t * alloc_fd_state(struct event_base *base, evutil_socket_t fd)
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
        event_new(base, fd, EV_WRITE, msg_send_nb, state);

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
	
	state->send_state = 0;//MINIMSG_STATE_SEND_NUMBER_OF_FRAME;
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
    return state;
}

void do_accept(evutil_socket_t listener, short event, void *arg)
{
    struct event_base *base = arg;
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
	printf("accept a new fd\n");
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        evutil_make_socket_nonblocking(fd);
        state = alloc_fd_state(base, fd);
        assert(state); /*XXX err*/
        assert(state->write_event);
        event_add(state->read_event, NULL);
    }
}



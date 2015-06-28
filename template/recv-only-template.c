#include <minimsg/minimsg.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <signal.h>
#define MAX_EVENTS 8
int main()
{
	minimsg_context_t* ctx;
	minimsg_socket_t* socket;
	msg_t * m;
	int efd,n,i;
	struct epoll_event event;
	struct epoll_event* events;
	sigset_t mask;
        int sfd;
        struct signalfd_siginfo fdsi;
        ssize_t s;

        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);

        /* Block signals so that they aren't handled
           according to their default dispositions */

        if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) handle_error("sigprocmask");

        sfd = signalfd(-1, &mask, 0);
        if (sfd == -1) handle_error("signalfd");
	
	efd = epoll_create1(0);
	if(efd == -1){
		perror("epoll_create");
		return 0;
	}

	ctx = minimsg_create_context();
	if(!ctx){
		fprintf(stderr,"fail to create minimsg context\n");
		return 0;
	}
	
	socket = minimsg_create_socket(ctx,MINIMSG_RECV_ONLY);
	fprintf(stderr,"socket is created\n");
	if(minimsg_bind(socket,"local:///home/bendog/git/minimsg/template/local") == MINIMSG_FAIL){
		fprintf(stderr,"bind fails\n");
		return 0;
	}
	else
		fprintf(stderr,"bind is OK\n");
/* add event */
	/* socket */
	event.data.fd = minimsg_socket_recv_fd(socket);
	event.events = EPOLLIN;
	s = epoll_ctl(efd,EPOLL_CTL_ADD,event.data.fd,&event);
        if(s == -1){
        	perror("epoll_ctl");
                return 0;
        }
	/* signal */
	event.data.fd = sfd;
	event.events = EPOLLIN;
	s = epoll_ctl(efd,EPOLL_CTL_ADD,event.data.fd,&event);
        if(s == -1){
        	perror("epoll_ctl");
                return 0;
        }
	events = calloc(MAX_EVENTS,sizeof( event ));
        if(!events)handle_error("calloc fails\n");
/* start */		
	while(1){
		n = epoll_wait(efd,events,MAX_EVENTS,-1);
		printf("got new events: %d\n",n);
		for(i = 0 ;i<n; i++){
                        if( minimsg_socket_recv_fd(socket) == events[i].data.fd){
				m = minimsg_recv(socket);
				printf("server receives message\n");
				msg_print(m);
				msg_free(m);
			}
			else if(sfd == events[i].data.fd){
				s = read(sfd,&fdsi,sizeof(struct signalfd_siginfo));
				//TODO , signal occurs again
				if( s != sizeof( struct signalfd_siginfo)) handle_error("read");
				if(fdsi.ssi_signo == SIGINT){
					printf("got signal SIGINT\n");
					goto end;
				}
			}
		}
	}
end:
	free(events);
	close(sfd);	
	close(efd);
	minimsg_free_context(ctx);
	return 0;
}

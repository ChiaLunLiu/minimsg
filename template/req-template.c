/*
 *	This program acts as client
 *	It uses MINIMSG_REQ socket type
 *	socket of type MINIMSG_REQ communicates data in the order of {send,recv}*
 *	Date: 2015/06/28
 */
#include <minimsg/minimsg.h>
#include <stdio.h>
#include <event2/event.h>

int main(int argc , char** argv)
{
	minimsg_context_t* ctx;
	minimsg_socket_t* socket;
	msg_t * m;
	int i;
	ctx = minimsg_create_context();
	if(!ctx){
		fprintf(stderr,"fail to create minimsg context\n");
		return 0;
	}
	printf("created\n");
	socket = minimsg_create_socket(ctx,MINIMSG_REQ);
	printf("socket is created\n");

	if(minimsg_connect(socket,"remote://127.0.0.1:12345") == MINIMSG_OK){
		printf("connected\n");
	}
	else{
		printf("fail to connect\n");
		return 0;
	}
	for(i=0;i<10 ; i++){
		printf("cleints sends message\n");
		m = msg_alloc();
		msg_append_string_f(m,"hi from client %s", (argc > 1) ? argv[1] : "");
		minimsg_send(socket,m);
	
		m = minimsg_recv(socket);
		/* network disconnect so return NULL */
		if(m == NULL) break;
		printf("client receives message\n");
		msg_print(m);
		msg_free(m);
	}
	printf("minimsg free socket\n");
	minimsg_free_socket(socket);
	printf("minimsg free context\n");
	minimsg_free_context(ctx);
	return 0;
}

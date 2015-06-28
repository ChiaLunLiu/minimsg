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
	socket = minimsg_create_socket(ctx,MINIMSG_SEND_ONLY);
	printf("socket is created\n");

	if(minimsg_connect(socket,"local:///home/bendog/git/minimsg/template/local") == MINIMSG_OK){
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
	}
	sleep(3);
	printf("minimsg free socket\n");
	minimsg_free_socket(socket);
	printf("minimsg free context\n");
	minimsg_free_context(ctx);
	return 0;
}

#include <minimsg/minimsg.h>
#include <stdio.h>
#include <event2/event.h>

int main(int argc , char** argv)
{
	minimsg_context_t* ctx;
	minimsg_socket_t* socket;
	msg_t * m;
	struct sockaddr_in server;
	int i;
	server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_family = AF_INET;
    server.sin_port = htons(12345);
	ctx = minimsg_create_context();
	if(!ctx){
		fprintf(stderr,"fail to create minimsg context\n");
		return 0;
	}
	printf("created\n");
	socket = minimsg_create_socket(ctx,MINIMSG_REQ);
	printf("socket is created\n");

	if(minimsg_connect(socket,server) == MINIMSG_OK){
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
	if(m == NULL) break;
	printf("client receives message\n");
	msg_print(m);
	msg_free(m);
	sleep(1);
}
	printf("minimsg free socket\n");
	minimsg_free_socket(socket);
	/* free all memory including minimsg_socket 
	*/
	printf("minimsg free context\n");
	minimsg_free_context(ctx);
	return 0;
}

#include <minimsg/minimsg.h>
#include <stdio.h>

int main()
{
	minimsg_context_t* ctx;
	minimsg_socket_t* socket;
	msg_t * m;
	ctx = minimsg_create_context();
	if(!ctx){
		fprintf(stderr,"fail to create minimsg context\n");
		return 0;
	}
	
	socket = minimsg_create_socket(ctx,MINIMSG_REP);
	fprintf(stderr,"socket is created\n");
	if(minimsg_bind(socket,12345) == MINIMSG_FAIL){
		fprintf(stderr,"bind fails\n");
		return 0;
	}
	else
		fprintf(stderr,"bind is OK\n");
		
	while(1){
	m = minimsg_recv(socket);
	printf("server receives message\n");
	msg_print(m);
	sleep(100);
	msg_free(m);
	printf("server sends message back\n");
	m = msg_alloc();
	msg_append_string(m,"hi from server");
	minimsg_send(socket,m);
	}
	
	minimsg_free_context(ctx);
	return 0;
}

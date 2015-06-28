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
	
	socket = minimsg_create_socket(ctx,MINIMSG_RECV_ONLY);
	fprintf(stderr,"socket is created\n");
	if(minimsg_bind(socket,"local:///home/bendog/git/minimsg/template/local") == MINIMSG_FAIL){
		fprintf(stderr,"bind fails\n");
		return 0;
	}
	else
		fprintf(stderr,"bind is OK\n");
		
	while(1){
		m = minimsg_recv(socket);
		printf("server receives message\n");
		msg_print(m);
		msg_free(m);
	}
	
	minimsg_free_context(ctx);
	return 0;
}

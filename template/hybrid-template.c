#include <minimsg/minimsg.h>
#include <stdio.h>
#include <pthread.h>
minimsg_socket_t* sk_server, * sk_client;

void* thread_func(void* arg){
	msg_t* m;
	int i;
	for(i=0;i<10 ; i++){
		printf("cleints sends message\n");
		m = msg_alloc();
		msg_append_string_f(m,"hi from client");
		minimsg_send(sk_client,m);
	
		m = minimsg_recv(sk_client);
		/* network disconnect so return NULL */
		if(m == NULL) break;
		printf("client receives message\n");
		msg_print(m);
		msg_free(m);
	}
	printf("thread ends\n");
	return NULL;
}
int main()
{
	minimsg_context_t* ctx;
	pthread_t pth;
	msg_t * m;
	struct sockaddr_in server_addr;
	int i;
	server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(12344);
	
	ctx = minimsg_create_context();
	if(!ctx){
		fprintf(stderr,"fail to create minimsg context\n");
		return 0;
	}
	
	sk_server = minimsg_create_socket(ctx,MINIMSG_REP);
	sk_client = minimsg_create_socket(ctx,MINIMSG_REQ);
	fprintf(stderr,"socket is created\n");
	if(minimsg_bind(sk_server,12345) == MINIMSG_FAIL){
		fprintf(stderr,"bind fails\n");
		return 0;
	}
	else
		fprintf(stderr,"bind is OK\n");
	
	if(minimsg_connect(sk_client,server_addr) == MINIMSG_OK){
		printf("connected\n");
	}
	else{
		printf("fail to connect\n");
		return 0;
	}
	pthread_create(&pth,NULL,thread_func,NULL);
	
	while(1){
	m = minimsg_recv(sk_server);
	printf("server receives message\n");
	msg_print(m);
	
	msg_free(m);
	printf("server sends message back\n");
	m = msg_alloc();
	msg_append_string(m,"hi from server");
	minimsg_send(sk_client,m);
	}
	
	pthread_join(pth,NULL);
	minimsg_free_socket(sk_client);
	minimsg_free_socket(sk_server);
	
	minimsg_free_context(ctx);
	return 0;
}

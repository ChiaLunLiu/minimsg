#include<stdio.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<signal.h>
#include<string.h>
#include "minimsg.h"
#define TIME 10
int main(int argc,char** argv)
{
	int sock;
	int r,i;
	const char* content;
	struct sockaddr_in server;
	frame_t * f;
	msg_t * m;
	char buf[204];
	int type = 2;
	const char* const str = "This is from client";
	sock = socket(AF_INET,SOCK_STREAM,0);
	if(sock == -1)
	{
		perror("could not create socket");
		return 1;
	}
	puts("Socket created");
	server.sin_addr.s_addr = inet_addr("127.0.0.1");
	server.sin_family = AF_INET;
	server.sin_port = htons(12345);
	
	if (connect(sock,(struct sockaddr*)&server,sizeof(server))<0)
	{
		perror("connect failed.");
		return 1;
	}
	puts("Connected");
		/* message testing */
		for(i = 0 ;i < 10; i++){
	//		printf("frame %d\n",i);
			m = msg_alloc();
			msg_append_string(m,"frame 1");
			msg_append_string(m,"frame 2");
			msg_append_string(m,"frame 3");
			msg_print(m);
			if(msg_send(sock,m) == MINIMSG_OK){
				printf("send OK\n");
			}
			else printf("send FAIL\n");
		
		}
		printf("start receiving ...\n");
		for(i=0;i<10;i++){
			printf("recv frame %d\n",i);
			if(msg_recv(sock,&m) == MINIMSG_OK){
				msg_print(m);
				msg_free(m);
			}
			else printf("recv FAIL\n");
		}
	close(sock);
	
	return 0;
}

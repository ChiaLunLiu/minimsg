#include<stdio.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<signal.h>
#include<string.h>
#include<minimsg/minimsg.h>
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
			printf("frame %d\n",i);
			m = msg_alloc();
			msg_append_string_f(m,"frame 1 %d",i);
			msg_append_string_f(m,"frame 2 %d",i);
			msg_append_string_f(m,"frame 3 %d",i);
			if(msg_send(sock,m) == MINIMSG_OK){
				printf("send OK\n");
			}
			else printf("send FAIL\n");
		
		}
	close(sock);
	
	return 0;
}

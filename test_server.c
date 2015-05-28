#include "minimsg.h"
#include <stdio.h>
#include <event2/event.h>

/* input : msg_t
 * output: msg_t or null
 * free input memory
 */ 
void* cb(void* arg)
{
	msg_t* m = (msg_t*)arg;
	printf("hi\n");
	
	msg_free(m);
	return NULL;
}
int main()
{
    int port = 12345;
    struct event_base *base;
    msg_server_t* server;
    base = event_base_new();
    if (!base)
        return; /*XXXerr*/
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(port);

	if(server = run_msg_server(base,port, cb,0)){
		puts("server ready");
	}
	else{
		puts("fail to start server, program will be terminated");
		return 0;
	}
     
    /* infinite loop */
    event_base_dispatch(base);
    
    free_msg_server(server);
    return 0;
}

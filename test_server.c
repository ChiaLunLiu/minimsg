#include "minimsg.h"
#include <stdio.h>
#include <event2/event.h>
#include <signal.h>
/* input : msg_t
 * output: msg_t or null
 * free input memory
 */ 
void* cb(void* arg)
{
	msg_t* m = (msg_t*)arg;

	printf("callback\n");
	return m;
}
int main()
{
    int port = 12345;
    msg_server_t* server;
    struct event_base *base;
    base = event_base_new();
    if (!base)
        return; /*XXXerr*/

    if(server = create_msg_server(base,port, cb,3)){
		puts("server is initiated");
    }
    else{
		puts("fail to start server, program will be terminated");
		return 0;
    }
    /* infinite loop */
    event_base_dispatch(base); 
    printf("the server is shut down\n");
    event_base_free(base);
    return 0;
}

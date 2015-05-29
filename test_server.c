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
	printf("hi\n");
	
//	msg_free(m);
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

    if(server = create_msg_server(base,port, cb,2)){
		puts("server ready");
    }
    else{
		puts("fail to start server, program will be terminated");
		return 0;
    }
    /* infinite loop */
    event_base_dispatch(base); 
    fprintf(stderr,"finish !!!\n");
    event_base_free(base);
    return 0;
}

#include <minimsg/minimsg.h>
#include <stdio.h>
#include <event2/event.h>
#include <signal.h>
/* cb is callback function when a message is received through network
 * user does not need to care who sends the message, the server would handle it
 * input 
 *  arg 1 type:msg_t*
 * output
 * possible values
 *    1.  NULL  ( the user has nothing to send it back
 *    2.  msg_t* if the user wants to send message back
 * 
 * user has to free input argument and alloc memory for return message.
 */ 
void* cb(void* arg)
{
	msg_t* m = (msg_t*)arg;
	printf("received\n");
	msg_print(m);
	msg_free(m);
	printf("send\n");
	m = msg_alloc();
	msg_append_string(m,"hi from server");
	msg_append_string(m,"");	
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

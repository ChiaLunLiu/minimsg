/*
 * the program can be terminated by sending SIGUSR1.
 */

#include <minimsg/minimsg.h>
#include <stdio.h>
#include <event2/event.h>
#include <signal.h>
#include <minimsg/util.h>
#define THREADS 3


/* schedule task based on message content
 * The return value determines which thread in thread pool to be assigned the task
 * Don't free the input or modify the message 
 * Note that return value of 0 means thread 1
 * return value of 1 means thread 2
 * etc
 * If the return value >= number of threads in thread pool or < 0, the task is scheduled
 * by default scheduler.
 */
int task_scheduler(const void* arg)
{
	const msg_t* m = (const msg_t*) arg;
	dbg("\n");
	/* use the following two API to peek the content
	 * int msg_number_of_frame(const msg_t* m);
	 * const char* msg_content_at_frame(const msg_t* m,int idx);
	 * idx starts from 0. 
	 */
	/* return [0,THREADS) */
	return 0;	
}
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
    /* if you don't want to use your own task scheduler, set it to NULL to use default scheduler */
    if(server = create_msg_server(base,port, cb,task_scheduler,THREADS)){
		puts("server is initiated");
    }
    else{
		puts("fail to start server, program will be terminated");
		event_base_free(base);
		return 0;
    }
    /* infinite loop */
    event_base_dispatch(base); 
    printf("the server is shut down\n");
    event_base_free(base);
    return 0;
}

#include "minimsg.h"
#include <stdio.h>
int callback(msg_t* m)
{
	printf("hi\n");
	return 0;
}
int main()
{
	runMsgServer(12345,callback);
	return 0;
}

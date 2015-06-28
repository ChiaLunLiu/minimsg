# minimsg

minimsg is a network library that helps to transmit data over TCP

TCP is streaming, minimsg does prefix-length to locate data TCP.

minimsg provides some basic socket model

1. REQ
2. REP
3. SEND_ONLY
4. RECV_ONLY

REQ transmit data in the order of {send,recv}*
REP transmit data in the order of {recv,send}*
SEND_ONLY only send
RECV_ONLY only recv


minimsg creates another thread to do network data transmission in nonblocking way.

the directory template in minimsg source code is good starting point for users to

understand how to uses the API in a quick way


Platform: Linux 
Compilation Prerequisite:
1.install libevent and libpthread
2. make
3. sudo make install


 
Note:

The programs in template directory has been examined by valgrind memcheck and it shows no

memory leak in my test.

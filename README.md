# minimsg

TCP is a streaming protocol. Thus, users have to define their own delimiter to tell the data boundary.

minimsg uses prefix length to locate data in TCP. 

  In minimsg, the sending and receiving unit is message

A message consists of frame(s). If you have key/value pair to send,

you can write key to first frame and value to second frame, and you don't

have to worry about how to distinguish their boundary.

  minimsg provides template server and client in directory template.

The server handles all the network I/O in nonblocking way and it is built

based on libevent to do multiplexing. The server comes with a thread pool.

Each thread in the thread pool has a message queue. To do load balance, the receiving message is

scheduled to send to thread with the least queue size. The server API provides

a callback for users to handle the message. This callback is shared by all threads.

 To run programs using minimsg, users can first run template server then template client.


Platform: Linux 
Compilation Prerequisite:
1.install libevent and libpthread
2. make
3. sudo make install


 
Note:

The programs in template directory has been examined by valgrind memcheck and it shows no

memory leak

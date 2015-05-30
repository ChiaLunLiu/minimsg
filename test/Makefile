DEBUG=
all:
	gcc -I../ send_then_recv_client.c ../minimsg.c ../ringbuffer.c ../thread_pool.c ../queue.c -o send_then_recv_client -O2 -levent
	gcc -I../ send_recv_interchange.c ../minimsg.c ../ringbuffer.c ../thread_pool.c ../queue.c -o send_recv_interchange -O2 -levent
clean:
	rm -f send_then_recv_client send_recv_interchange

DEBUG=
all:
	gcc -I../ send_then_recv_client.c ../minimsg.c ../ringbuffer.c ../util.c ../thread_pool.c ../queue.c -o send_then_recv_client -O2 -levent
	gcc -I../ send_recv_interchange.c ../minimsg.c ../ringbuffer.c ../util.c ../thread_pool.c ../queue.c -o send_recv_interchange -O2 -levent
	gcc -I../ blocking-client-template.c ../minimsg.c ../ringbuffer.c ../util.c ../thread_pool.c ../queue.c -o blocking-client-template -O2 -levent
clean:
	rm -f send_then_recv_client send_recv_interchange blocking-client-template

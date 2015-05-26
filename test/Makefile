DEBUG=
all:
	gcc -I../ send_then_recv_client.c ../minimsg.c ../ringbuffer.c ../queue.c -o send_then_recv_client -O2 -levent
clean:
	rm -f send_then_recv_client

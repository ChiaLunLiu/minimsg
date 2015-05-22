DEBUG=
all:
	gcc test_client.c minimsg.c queue.c ringbuffer.c -o client -O2 -levent
	gcc test_server.c minimsg.c queue.c ringbuffer.c -o server -O2 -levent
ringbuf:
	gcc ringbuffer_test.c ringbuffer.c -g
debug:
	gcc test_client.c minimsg.c -o client -O2 -pg
	gcc test_server.c minimsg.c -o server -O2
test:
	gcc nonblocking-server.c -o server -O2 ${DEBUG} -levent
test_server:
	gcc test_server.c minimsg.c -o server -O2 ${DEBUG} -levent
clean:
	rm -f client server

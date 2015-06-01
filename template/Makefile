
DEBUG=#-pg -DDEBUG
all:
	gcc  nonblocking-server-template.c -o nonblocking-server-template -O2 -lminimsg ${DEBUG}
	gcc  blocking-client-template.c  -o blocking-client-template -O2  -lminimsg ${DEBUG}
clean:
	rm -f nonblocking-server-template blocking-client-template

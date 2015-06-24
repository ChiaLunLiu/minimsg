
DEBUG= #-DDEBUG=1
all:
	gcc client-template.c -o client -O2 -lminimsg -levent ${DEBUG}
	gcc server-template.c -o server -O2 -lminimsg -levent ${DEBUG}
	
all2:
	gcc  nonblocking-server-template.c -o nonblocking-server-template -O2 -lminimsg -levent ${DEBUG}
	gcc  blocking-client-template.c  -o blocking-client-template -O2  -lminimsg ${DEBUG}
	gcc  hybrid-template.c  -o hybrid-template -O2  -lminimsg ${DEBUG}
clean:
	rm -f nonblocking-server-template blocking-client-template hybrid-template


DEBUG= #-DDEBUG=1
all:
	gcc client-template.c -o client -O2 -lminimsg -levent ${DEBUG}
	gcc server-template.c -o server -O2 -lminimsg -levent ${DEBUG}
	gcc hybrid-template.c -o hybrid -O2 -lminimsg -levent ${DEBUG}
	
clean:
	rm -f client server hybrid

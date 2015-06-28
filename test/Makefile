
DEBUG= #-DDEBUG=1
all:
	gcc lightweight-client-template.c -o client -O2 -lminimsg -levent ${DEBUG}
	gcc recv-only-template.c -o recv-only -O2 -lminimsg -levent ${DEBUG}
clean:
	rm -f client recv-only

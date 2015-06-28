
DEBUG= #-DDEBUG=1
all:
	gcc client2.c -o client2 -O2 -lminimsg -levent ${DEBUG}
	gcc send-only-template.c -o client -O2 -lminimsg -levent ${DEBUG}
clean:
	rm -f client2 hybrid1 hybrid2


DEBUG= #-DDEBUG=1
all:
	gcc client2.c -o client2 -O2 -lminimsg -levent ${DEBUG}
	gcc hybrid1.c -o hybrid1 -O2 -lminimsg -levent ${DEBUG}
	gcc hybrid2.c -o hybrid2 -O2 -lminimsg -levent ${DEBUG}
clean:
	rm -f client2 hybrid1 hybrid2

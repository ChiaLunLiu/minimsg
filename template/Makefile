
DEBUG= #-DDEBUG=1
all:
	gcc req-template.c -o req -O2 -lminimsg -levent ${DEBUG}
	gcc lightweight-client-template.c -o lightweight-client -O2 -lminimsg -levent ${DEBUG}
	gcc rep-template.c -o rep -O2 -lminimsg -levent ${DEBUG}
	gcc send-only-template.c -o send-only -O2 -lminimsg -levent ${DEBUG}
	gcc recv-only-template.c -o recv-only -O2 -lminimsg -levent ${DEBUG}
	gcc hybrid-template.c -o hybrid -O2 -lminimsg -levent ${DEBUG}
	
clean:
	rm -f req rep send-only recv-only lightweight-client hybrid


MINIMSG_INCLUDE_DIR=/usr/include/minimsg

all: library
library:
	gcc -fPIC minimsg.c queue.c ringbuffer.c thread_pool.c util.c -shared -O2 -o libminimsg.so -lpthread -levent
install:
	install libminimsg.so /usr/lib
	mkdir -p ${MINIMSG_INCLUDE_DIR}
	install  minimsg.h ${MINIMSG_INCLUDE_DIR}
	install  queue.h ${MINIMSG_INCLUDE_DIR}
	install  ringbuffer.h ${MINIMSG_INCLUDE_DIR}
	install  thread_pool.h ${MINIMSG_INCLUDE_DIR}
	install  util.h ${MINIMSG_INCLUDE_DIR}
uninstall:
	rm /usr/lib/libminimsg.so
	rm ${MINIMSG_INCLUDE_DIR}/minimsg.h
	rm ${MINIMSG_INCLUDE_DIR}/queue.h
	rm ${MINIMSG_INCLUDE_DIR}/ringbuffer.h
	rm ${MINIMSG_INCLUDE_DIR}/thread_pool.h
	rm ${MINIMSG_INCLUDE_DIR}/util.h
	rmdir ${MINIMSG_INCLUDE_DIR}
	
template:
	make -C template
clean:
	rm libminimsg.so
	make -C template clean
distclean: uninstall clean
	
	


MINIMSG_INCLUDE_DIR=/usr/include/minimsg
DEBUG=#-DDEBUG
all: library
library:
	gcc -fPIC ${DEBUG} list.c list_iterator.c list_node.c  minimsg.c queue.c ringbuffer.c  util.c -shared -O2 -o libminimsg.so -lpthread -levent
install:
	install libminimsg.so /usr/lib
	mkdir -p ${MINIMSG_INCLUDE_DIR}	
	install  minimsg.h ${MINIMSG_INCLUDE_DIR}
	install  queue.h ${MINIMSG_INCLUDE_DIR}
	install  ringbuffer.h ${MINIMSG_INCLUDE_DIR}
	install  util.h ${MINIMSG_INCLUDE_DIR}
	install  list.h ${MINIMSG_INCLUDE_DIR}
uninstall:
	rm /usr/lib/libminimsg.so
	rm ${MINIMSG_INCLUDE_DIR}/minimsg.h
	rm ${MINIMSG_INCLUDE_DIR}/queue.h
	rm ${MINIMSG_INCLUDE_DIR}/ringbuffer.h
	rm ${MINIMSG_INCLUDE_DIR}/util.h
	rm ${MINIMSG_INCLUDE_DIR}/list.h
	rmdir ${MINIMSG_INCLUDE_DIR}
	
template:
	make -C template
clean:
	rm libminimsg.so
	make -C template clean
distclean: uninstall clean
	
	

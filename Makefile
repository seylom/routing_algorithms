all: manager distvec
manager: manager.c protocol_helper.c
	gcc -g -pthread -lrt -w -o manager protocol_helper.c manager.c
distvec: distvec.c protocol_helper.c
	gcc -g -pthread -lrt -w -o distvec protocol_helper.c distvec.c
clean:
	rm -rf *o manager distvec

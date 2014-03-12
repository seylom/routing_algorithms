all: manager distvec linkstate
manager: manager.c protocol_helper.c
	gcc -g -pthread -lrt -w -o manager protocol_helper.c manager.c
distvec: distvec.c protocol_helper.c
	gcc -g -pthread -lrt -w -o distvec protocol_helper.c distvec.c
linkstate: linkstate.c protocol_helper.c
	gcc -g -pthread -lrt -w -o linkstate protocol_helper.c linkstate.c
clean:
	rm -rf *o manager distvec linkstate

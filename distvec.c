/*
** distvec.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#define MANAGER_PORT "5555"
#define NODE_PORT "6666"
#define BACKLOG 10

#include <pthread.h>
#include "protocol_helper.h"

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
*   UDP handler
*/
void *distvec_udp_handler(void * pvdata){
    
}

int main(int argc, char *argv[])
{
	int node_tcp, node_udp;

	int numbytes, filelen;
	char buf[MAX_DATA_SIZE];
	struct sockaddr_storage node_addr;
	socklen_t addr_len;
	struct addrinfo manager_hints, node_hints, *managerinfo, *nodeinfo, *p, *q;
	int rv;
	int yes=1;
	char s[INET6_ADDRSTRLEN];

	if (argc < 2) {
	    fprintf(stderr,"usage: %s managerhostname\n", argv[0]);
	    exit(1);
	}
	
	node_tcp = setup_tcp_connection(argv[1], MANAGER_PORT);
	
	char *nodePort = (argc >= 3)? argv[2] : NODE_PORT;
	
	//create a node item.
    node_info *item = malloc(sizeof(node_info));
    
    item->host = malloc(INET6_ADDRSTRLEN);
    item->host[0] = '\0';
    item->port = malloc(50);
    item->port[0] = 0;
    
	item->tcp_socketfd = node_tcp;
	
	//strcpy(item->host, "localhost");
	
	//create a thread to handle communication with manager
	pthread_t thread;
	pthread_create(&thread, NULL, (void*)node_to_manager_handler, (void*)item);

	while(1){
/*            if ((numbytes = recvfrom(node_udp, buf, MAX_DATA_SIZE-1 , 0,*/
/*                (struct sockaddr *)&node_addr, &addr_len)) == -1) {*/
/*                perror("recvfrom");*/
/*                exit(1);*/
/*            }*/
/*            */
/*            //grab the data and send it to a new thread*/
/*            //TODO: modify to pass a structure as a parameter of the thread with a copy of the message*/
/*            */
/*            pthread_t thread;*/
/*            pthread_create(&thread, NULL, (void*)distvec_udp_handler, (void*)&node_udp);*/
	}

	close(node_udp);
    close(node_tcp);
    
	return 0;
}


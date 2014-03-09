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

int virtual_id = 0;

struct node_data *nd_data;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// start convergence process.
void build_network_map(){

    if(!nd_data){
        return;
    }
    
    node_info *node = nd_data->node;
    
    if (node)
        printf("Starting convergence process for node %d\n", node->id);
    
    //send node distance vector view to the world.
}

void handle_udp_connections(){

}

void initialize_data_container(node_data *ndata){
    nd_data = malloc(sizeof(node_data));
	nd_data->neighbours = malloc(sizeof(item_list));
	nd_data->neighbours->head = NULL;
	nd_data->neighbours->tail = NULL;
    nd_data->neighbours->count = 0;
    
    nd_data->protocol_handler = build_network_map;
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
	
	//char *nodePort = (argc >= 3)? argv[2] : NODE_PORT;
	initialize_data_container(nd_data);

	//create a node item.
    node_info *item = malloc(sizeof(node_info));
    
    item->host = malloc(INET6_ADDRSTRLEN);
    item->host[0] = '\0';
    item->port = malloc(50);
    item->port[0] = 0;
    
	item->tcp_socketfd = node_tcp;

	nd_data->node = item;
	
	//strcpy(item->host, "localhost");
	
	//create a thread to handle communication with manager
	pthread_t thread;
	pthread_create(&thread, NULL, (void*)node_to_manager_handler, (void*)nd_data);

	while(1){
        //actually, there is nothing here since the thread handling TCP connections
        //waits on virtual id assignment to kick off UDP port reading (port_no = virtual_id + 7000)
	}

	close(node_udp);
    close(node_tcp);
    
	return 0;
}


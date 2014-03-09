//  protocol_helper.c

#include "protocol_helper.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>


//sends the provided message through the socket descriptor provided.
void send_message(int socketfd, char* message){
	//int n = write(socketfd, message, strlen(message)) ;
	int n = send(socketfd, message, strlen(message), 0) ;
	
	if (n < 0){
		perror("write");
		exit(1);
	}
}

/*
* Establishes a TCP connection with host using the provided port
*/
int setup_tcp_connection(char* host, char* port){

	int node;
	int rv;
	char* s;

	struct addrinfo manager_hints, *managerinfo, *p;

	memset(&manager_hints, 0, sizeof manager_hints);
	manager_hints.ai_family = AF_UNSPEC;
	manager_hints.ai_socktype = SOCK_STREAM;

	/* connect to manager*/
	if ((rv = getaddrinfo(host, port, &manager_hints, &managerinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = managerinfo; p != NULL; p = p->ai_next) {
		if ((node = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("distvec: socket");
			continue;
		}

		//connect the node host to the manager host.
		if (connect(node, p->ai_addr, p->ai_addrlen) == -1) {
			close(node);
			perror("distvec: unable to connect to manager");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "distvec: failed to connect\n");
		return 2;
	}

	//inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
	//printf("distvec: connecting to %s\n", s);

	freeaddrinfo(managerinfo); // all done with this structure

	return node;
}


/*
*   Establishing a UDP connection
*/
int setup_udp_connection(char* host, char* port){

    int node;
    struct addrinfo node_hints, *nodeinfo, *q;
    int rv;
    int yes=1;
    
    memset(&node_hints, 0, sizeof node_hints);
	node_hints.ai_family = AF_UNSPEC;
	node_hints.ai_socktype = SOCK_DGRAM;

	if ((rv = getaddrinfo(host, port, &node_hints, &nodeinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(q = nodeinfo; q != NULL; q = q->ai_next) {
		if ((node = socket(q->ai_family, q->ai_socktype,
				q->ai_protocol)) == -1) {
			perror("node: socket");
			continue;
		}

		if (setsockopt(node, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(node, q->ai_addr, q->ai_addrlen) == -1) {
			close(node);
			perror("node: bind");
			continue;
		}

		break;
	}

	if (q == NULL)  {
		fprintf(stderr, "node: failed to bind\n");
		return 2;
	}

	freeaddrinfo(nodeinfo);
	
	return node;
}

/*
*   Extract virtual id and cost for the neighbour in the message
*/
neighbour *extract_neighbor(char *message){
	if (message == NULL)
		return NULL;
	
	const char info_delimiters[] = ":";
	
	neighbour *nb = malloc(sizeof(neighbour));
	
	char *token, *running;
	running = strdup(message);
	
	token = strsep(&running, info_delimiters);
	nb->id = atoi(token);
		
	token = strsep(&running, info_delimiters);
	nb->cost = atoi(token);

	return nb;
}

/*
*   extract all neighbours provided after first connection with manager
*/
item_list *extract_neighbors_info(char *message){
	
	if (message == NULL)
		return NULL;
		
	const char array_delimiters[] = "|";
	item_list *list = malloc(sizeof(item_list));
	
	char *token, *running;
	running = strdup(message);
	token = strsep(&running, array_delimiters);
	
	while(token){
		neighbour *nb = extract_neighbor(token);
		
		if (nb){
			add_to_list(list, (void*)nb);
		}else{
		    break;
		}
		
		token = strsep(&running, array_delimiters);
	}
	
	return list;
}


/*
* Add an item to the list
*/
void add_to_list(item_list *list, void* item){
	if (!item)
		return;
	
	if (list == NULL){
		list = malloc(sizeof(item_list));
		list->tail = NULL;
		list->count = 0;
	}
	
	item_link *link = malloc(sizeof(item_link));
	link->data = item;
	

	if(!list->head){
		list->head = link;
		list->tail = list->head;
	}else{
		list->tail->next = link;
		list->tail = list->tail->next;
	}
	
	list->tail->next = NULL;
	list->count += 1;
}

/*
*   Extract full node information from message sent to all neighbour upon
*   a new node joining.
*/
node_info *extract_node_information(char *message){
	
	const char delimiters[] = "|";
	
	if (message == NULL)
		return NULL;
		
	struct node_info *node =  malloc(sizeof(struct node_info));
	node->host = malloc(INET6_ADDRSTRLEN);
	node->host[0] = '\0';
	
	node->port = malloc(256);
	node->port[0] = '\0';
	
	char *token, *running;
	running = strdup(message);
		
	token = strsep (&running, delimiters);
	node->id = atoi(token);
	
	token = strsep (&running, delimiters);
	strcpy(node->host, token);
	
	token = strsep (&running, delimiters);
	strcpy(node->port, token);
	
	return node;
}

void build_network_map(){
    printf("Starting convergence process...\n");
}

/*
*   handles node connection to the manager and message received
*/
void *node_to_manager_handler(void* pvnode_info){
    node_info *node = (node_info*)pvnode_info;
    char buffer[256];
    int numbytes;
    
    //First contact
    //send contact message and receive information back
    const char delimiters[] = "|";
    
    int num_chars = sprintf(buffer, "%s|%s", MESSAGE_CONTACT, node->port);
    
    buffer[num_chars] = '\0';
    
    send_message(node->tcp_socketfd, buffer);
    
    while(1){
    
        bzero(buffer, 256);
        
        if ((numbytes = read(node->tcp_socketfd, buffer, sizeof(buffer))) < 0) {
			perror("read");
		    	exit(1);
		}
		
		//printf("message-received: %s\n", buffer);
		
		char *token, *running;
		running = strdup(buffer);
		
		token = strsep (&running, delimiters);
		
		if (strcmp(token, MESSAGE_NEIGHBOURS) == 0){ //neighbor message
			
			item_list *nbs = extract_neighbors_info(running);
			
			if (nbs){	
				item_link *p = nbs->head;
				
				while(p){
					neighbour *nb = (neighbour*)p->data;				
					printf("now linked to node %d with cost %d\n", nb->id, nb->cost);
					p = p->next;
				}
			}
			
			bzero(buffer, 256);
			int numnext = sprintf(buffer,"%s|%s", MESSAGE_ACK, MESSAGE_SEND_INFO );
			buffer[numnext] = '\0';
			
			send_message(node->tcp_socketfd, buffer);
			
		}
		else if (strcmp(token, MESSAGE_NODE_INFO) == 0){ //node info message
			node_info *nb = extract_node_information(running);
			
			//printf("Node information received: id->%d host->%s port->%s\n", 
			//	nb->id, nb->host, nb->port);
			
			bzero(buffer, 256);
			int numnext = sprintf(buffer,"%s|%s", MESSAGE_ACK, MESSAGE_SEND_DATA );
			buffer[numnext] = '\0';
			
			send_message(node->tcp_socketfd, buffer);
		}
		else if (strcmp(token, MESSAGE_DATA) == 0){
		    //not yet implemented
		}
		else if (strcmp(token, MESSAGE_START_CONVERGENCE) == 0){
		    build_network_map();
		}
    }
}
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

//function pointer 
void (*node_callback)();
void (*message_router)(char*, int);
void (*topology_change_callback)();     
       
//sends the provided message through the socket descriptor provided.
void send_message(int socketfd, char* message){
	//int n = write(socketfd, message, strlen(message)) ;
	int n = send(socketfd, message, strlen(message), 0) ;
	
	if (n < 0){
		perror("write");
		exit(1);
	}
}

//sends the provided message through the socket descriptor provided.
int send_message_with_ack(int socketfd, char* message){
	//int n = write(socketfd, message, strlen(message)) ;
	int n = send(socketfd, message, strlen(message), 0) ;
	
	if (n < 0){
		perror("write");
		exit(1);
	}
	
	int numbytes;
	char buffer[50];
	bzero(buffer, 0);
	
	if ((numbytes = read(socketfd, buffer, sizeof buffer)) < 0) {
		perror("recv"); 
	}
	
	if (strcmp(buffer, MESSAGE_ACK) == 0)
	    return 1;
	    
	return 0;
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
	manager_hints.ai_family = AF_INET;
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
	//node_hints.ai_family = AF_UNSPEC;
	//node_hints.ai_family = AF_INET;
	node_hints.ai_socktype = SOCK_DGRAM;
	node_hints.ai_family = AF_INET;
	node_hints.ai_flags = AI_PASSIVE;

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
*   sends UDP message
*/
void send_udp_message(char *host, char *port, char *message){

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("node: socket");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "node: failed to bind socket\n");
        return 2;
    }

    if ((numbytes = sendto(sockfd, message, strlen(message), 0,
             p->ai_addr, p->ai_addrlen)) == -1) {
        perror("node: sendto");
        exit(1);
    }

    freeaddrinfo(servinfo); 
    
    close(sockfd);
}


data_message *extract_message(char *message, int from_neighbour){
    
    if (message == NULL)
		return NULL;
	
	const char info_delimiters[] = "|";
	
	data_message *mess = malloc(sizeof(data_message));
	mess->message = malloc(MAX_BUFFER_SIZE);
	mess->message[0] = 0;
	
	mess->path = malloc(MAX_BUFFER_SIZE);
    mess->path[0] = 0;
	
	char *token, *running;
	running = strdup(message);
	
	token = strsep(&running, info_delimiters);
	mess->source_id = atoi(token);
	
	token = strsep(&running, info_delimiters);
	mess->dest_id = atoi(token);
		
	if (from_neighbour){
	    token = strsep(&running, info_delimiters);
	    strncpy(mess->path,token, strlen(token));
	}
	
	token = strsep(&running, info_delimiters);
	strncpy(mess->message,token, strlen(token));


	return mess;
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

void create_list(item_list **list){
	item_list *nlist = malloc(sizeof(item_list));
	nlist->head = NULL;
	nlist->tail = NULL;
	nlist->count = 0;
	*list = nlist;
} 

/*
* Add an item to the list
*/
void add_to_list(item_list *list, void* item){
	if (!item)
		return;
	
	item_link *link = malloc(sizeof(*link));
	
    if(link == NULL){
        fprintf(stderr, "Unable to allocate memory for new link\n");
        exit(-1);
    }

    link->data = item;
	link->next = NULL;
	
    if (list->head == NULL){
	    list->head = link;
	    list->count +=1;
	    
	    return;
	}

    item_link *p = list->head;
    
    while (p) { 
        if(p->next == NULL)
        {
            p->next = link; 
            break; 
        }
        p = p->next;
    };
    
	list->count += 1;
}

void delete_list(item_link** head)
{
   item_link* current = *head;
   item_link* next;
 
   while (current != NULL) 
   {
       next = current->next;
       free(current);
       current = next;
   }
   
   *head = NULL;
}

int update_neighbours_cost(item_list* list, int u, int cost){
       
    item_link *p = list->head;
    int updated = 0;
    
    for(p;p!=NULL;p=p->next){
    
        neighbour *nb = (neighbour*)p->data;
        
        if (nb->id == u){
            nb->cost = cost;
            updated = 1;   
        }
    }
    
    if (!updated){
        neighbour *nb = malloc(sizeof(*nb));
        nb->id = u;
        nb->cost = cost;

        add_to_list(list, nb);
    }
    
    return updated;
}

int update_neighbours_list(item_list* list, node_info *node){
       
    item_link *p = list->head;
    int updated = 0;
    
    for(p;p!=NULL;p=p->next){
    
        node_info *nb = (node_info*)p->data;
        
        if (nb->id == node->id) 
            return 0 ;
    }
    
    add_to_list(list, node);
    
    return 1;

}

void remove_from_neighbours_cost(item_list *list, int id){  
    item_link *p,*q; 
    p = list->head;
    
    if (p == NULL)
        return;

    neighbour *n = (neighbour*)p->data;
    
    if (n->id == id){
        free(p->data); 
        list->head = list->head->next;
        list->count = 0;
        return;
    }
        
    q = p->next;
    
    while(q){
    
        neighbour *nb = (neighbour*)q->data;
        
        if (nb->id == id){       
             free(q->data);
             p->next = q->next;
             list->count -= 1;
             free(q);
             break;
        }
        
        q = q->next;
        p = p->next;
    }
}

void remove_from_neighbours(item_list *list, int id){
    item_link *p,*q; 
    
    p = list->head;
    
    
    if (p == NULL)
        return;
    

    node_info *n = (node_info*)p->data;
    
    if (n->id == id){
        free(p->data); 
        list->head = list->head->next;
        list->count -= 1;
        return;
    }
       
    q = p->next;
    
    while(q){
    
        node_info *nb = (node_info*)q->data;
        
        if (nb->id == id){       
             free(q->data);
             p->next = q->next;
             list->count -= 1;
             free(q);
             break;
        }
        
        q = q->next;
        p = p->next;
    }
    
    item_link *r = list->head;
    
    while(r){
        node_info *s = (node_info *)r->data;
        r = r->next;
    }
}

/*
*   Extract full node information from message sent to all neighbour upon
*   a new node joining.
*/
node_info *extract_node_information(char *message){
	
	const char delimiters[] = "|";
	
	if (message == NULL)
		return NULL;
		
	struct node_info *node =  malloc(sizeof(*node));
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

/*
*   handles UDP packet messages
*/
void *udp_message_handler(void* pvdata){
    udp_message *mess_info = (udp_message*)pvdata;
    
    //printf("%s\n", mess_info->message);
}

/*
*   Waits for data on a port and spawn of a thread to handle
*   incoming data and socket information
*/
void initialize_udp_listening(char* pvndtata){

    int numbytes;
    struct sockaddr_storage node_addr;
    char buf[MAX_BUFFER_SIZE];
    socklen_t addr_len;
    
    node_data *ndata = (node_data*) pvndtata;
    node_info *node = ndata->node;
    
    if (!ndata->udp_handler)
        ndata->udp_handler = udp_message_handler;
   
    //struct sockaddr_storage node_addr;
    //int numbytes;
    node->udp_socketfd = setup_udp_connection(NULL, node->port);
    
    while(1){
        //wait for data and upon receival, span of yet another
        //thread to handle it and keep the socket from being busy
        
        if ((numbytes = recvfrom(node->udp_socketfd, buf, sizeof buf , 0,
                (struct sockaddr *)&node_addr, &addr_len)) == -1) {
                perror("recvfrom");
                exit(1);
        }
        
        udp_message *message_info = malloc(sizeof(udp_message));
        message_info->message = malloc(MAX_BUFFER_SIZE);
        strncpy(message_info->message, buf, numbytes);
        
        message_info->source = NULL;
        
        pthread_t thread;
        pthread_create(&thread, NULL, (void*)ndata->udp_handler, (void*)message_info);
    }
}

void cleanup(node_data **data){
    
    if ((*data)->neighbours)
        delete_list(&(((*data)->neighbours)->head));
        
    if ((*data)->neighbours_cost)
        delete_list(&(((*data)->neighbours_cost)->head));
        
    if ((*data)->messages)    
        delete_list(&(((*data)->messages)->head));
    
    free(*data);
   
    *data = NULL;
}

/*
*   handles node connection to the manager and message received
*/
void *node_to_manager_handler(void* pvnd_data){

    char buffer[MAX_BUFFER_SIZE];
    int numbytes; 
    
    node_data *nd_data = (node_data*)pvnd_data;
    node_info *node = nd_data->node;
    
	item_list *neighbours_list = nd_data->neighbours;
	item_list *neighbours_cost_list = nd_data->neighbours_cost;
    
    //First contact
    //send contact message and receive information back
    const char delimiters[] = "|";
    
    int num_chars = sprintf(buffer, "%s|req", MESSAGE_CONTACT);
    
    buffer[num_chars] = '\0';
    
    send_message(node->tcp_socketfd, buffer);
 
    while(1){
    
        bzero(buffer, MAX_BUFFER_SIZE);
        
        if ((numbytes = read(node->tcp_socketfd, buffer, sizeof(buffer))) < 0) {
			perror("read");
		    	exit(1);
		}
		
		if(numbytes == 0){
		    //printf("Manager socket closed - exiting.\n");
		    //cleanup(&nd_data);
 
		    break;
		}
		
		char *token, *running;
		running = strdup(buffer);
		
		token = strsep (&running, delimiters);
		
		if (strcmp(token, MESSAGE_TOPO_INFO) == 0){ //info about topo and neighbors
			
			//The message should look like the following
			//MESSAGE_TOPO_INFO|assigned_id|id1:cost1|id2:cost2|id3:cost3|...
			//extract our virtual id
			token = strsep (&running, delimiters);
			node->id = atoi(token);
			
			int port = atoi(NODE_PORT) + node->id; // e.j (7000 + 1) for first node
			int num_port_chars = sprintf(node->port, "%d", port);
			node->port[num_port_chars] = 0; 

			//now that we have the port, start a thread for UDP port communications
			pthread_t thread;
			pthread_create(&thread, NULL, (void*)initialize_udp_listening, (void*)nd_data);
			
			//extract neighbours from the rest of the data
			item_list *nbs = extract_neighbors_info(running);
			
			if (nbs){	
				item_link *p = nbs->head;
				
				while(p){
					neighbour *nb = (neighbour*)p->data;	
					
					add_to_list(neighbours_cost_list, nb);
								
					printf("now linked to node %d with cost %d\n", nb->id, nb->cost);
					p = p->next;
				}
			}
			
			bzero(buffer, MAX_BUFFER_SIZE);
			int numnext = sprintf(buffer,"%s|%s", MESSAGE_ACK, MESSAGE_SEND_INFO );
			buffer[numnext] = '\0';
			
			send_message(node->tcp_socketfd, buffer);
			
		}
		else if (strcmp(token, MESSAGE_NODE_INFO) == 0){ //node info message
			
			
			node_info *nb = extract_node_information(running);
			
			if (!update_neighbours_list(neighbours_list, nb)){
			    free(nb);
			}
			
			if (neighbours_list->count == neighbours_cost_list->count){
				 bzero(buffer, MAX_BUFFER_SIZE);
				 int numnext = sprintf(buffer,"%s|%s", MESSAGE_ACK, MESSAGE_INFO_RECEIVED );
				 buffer[numnext] = '\0';
				
				 send_message(node->tcp_socketfd, buffer);
			}
		}
		else if (strcmp(token, MESSAGE_DATA) == 0){
		    
		    //send ack
		    bzero(buffer, MAX_BUFFER_SIZE);
			int numnext = sprintf(buffer,"%s|ok", MESSAGE_ACK);
			buffer[numnext] = '\0';
			send_message(node->tcp_socketfd, buffer);
			
		    message_router = nd_data->route_message_handler;
		    message_router(running, 0);
		}
		else if (strcmp(token, MESSAGE_START_CONVERGENCE) == 0){

		    node_callback = nd_data->protocol_handler;
		    node_callback();
		}
		else if (strcmp(token, MESSAGE_TOPO_UPDATE) == 0){

			neighbour *nb = extract_neighbor(running);
			
			if (nb->cost >= 0){
			    
			    int exists = update_neighbours_cost(nd_data->neighbours_cost, nb->id, nb->cost);
			    
			    printf("now linked to node %d with cost %d\n", nb->id, nb->cost);
			    
			    if (exists == 0){
			        //request node info for the neighbour we don't have
			        bzero(buffer, MAX_BUFFER_SIZE);
			        int req_numchars = sprintf(buffer,"%s|%d", MESSAGE_REQUEST_NODE_INFO, nb->id);
			        buffer[req_numchars] = '\0';
			        send_message(node->tcp_socketfd, buffer);
			        
			        sleep(1);
			    }
			}
			else
			{
		        //remove link
		        remove_from_neighbours_cost(nd_data->neighbours_cost, nb->id);
		        remove_from_neighbours(nd_data->neighbours, nb->id);
			    printf("no longer linked to node %d\n", nb->id);
			}
			
			bzero(buffer, MAX_BUFFER_SIZE);
			int numnext = sprintf(buffer,"%s|%s", MESSAGE_ACK, MESSAGE_TOPO_UPDATED );
			buffer[numnext] = '\0';
			
			send_message(node->tcp_socketfd, buffer);
		}
		else if (strcmp(token, MESSAGE_FLUSH_TOPO) == 0){
			
		    topology_change_callback = nd_data->topology_change_handler;
		    
		    if (topology_change_callback)
			    topology_change_callback();
			
			//send ack
			bzero(buffer, MAX_BUFFER_SIZE);
			int numnext = sprintf(buffer,"%s|%s", MESSAGE_ACK, MESSAGE_TOPO_FLUSHED );
			buffer[numnext] = '\0';
			
			send_message(node->tcp_socketfd, buffer);
		}
    }
    
    pthread_exit(NULL);
}

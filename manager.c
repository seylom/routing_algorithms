/*
** manager.c  The manager program
*/

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
#include <sys/time.h>
#include <pthread.h>

#include "protocol_helper.h"

#define BACKLOG 10	 // how many pending connections queue will hold

#define MAX_BUFFER 1024

#define MANAGER_PORT "5555"

pthread_mutex_t mutex_node_id;
pthread_mutex_t mutex_node_list;
pthread_mutex_t mutex_message_list;
pthread_mutex_t mutex_ready_ack;
pthread_mutex_t mutex_converged_ack;

int setup_completed = 0;
int expected_connections = 0;
int expected_topo_ack = 0;
int next_available_id = 1;
int info_received_ack_count = 0;
int converged_ack_count = 0;

struct item_list *client_nodes;
struct item_list *edges;
struct item_list *messages;

void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// get port, IPv4 or IPv6:
in_port_t get_in_port(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return (((struct sockaddr_in*)sa)->sin_port);
    }

    return (((struct sockaddr_in6*)sa)->sin6_port);
}

//handler for the created thread
void *thread_handler(void* pv_nodeitem){
	node_info *node = (node_info*)pv_nodeitem;
	char buffer[MAX_BUFFER_SIZE];
	int numbytes;
	const char delimiters[] = "|";

	while(1){
	    
	    bzero(buffer,MAX_BUFFER_SIZE);

	    if ((numbytes = read( node->tcp_socketfd, buffer, sizeof buffer)) < 0) {
		 	perror("recv");
		 	pthread_exit((void*) 0);
		}
		
		if (numbytes == 0){
		    break;
		}
		
		buffer[numbytes] = '\0';
		
		//Message format expected:  MESSAGE_TYPE|PAYLOAD
		//Type of payloads: MESSAGE_CONTACT|PortNumber
		//                  MESSAGE_ACK|MESSAGE_SEND_INFO (to ask for neighbors info)
		//                  MESSAGE_ACK|MESSAGE_SEND_DATA (to get messages to be sent)
		//                  MESSAGE_NEIGHBOURS|data
		//                  
		char *token, *running;
		running = strdup(buffer);	
		token = strsep (&running, delimiters);
		
		if (strcmp(token, MESSAGE_CONTACT) == 0){  //contact message
		    
		    //token = strsep (&running, delimiters);

		    //Gets a uniquely assigned virtual id assigned by manager and 
		    //used to identify nodes in the vitual topology
            request_virtual_id(&(node->id));
            
            //add node to manager list of node_info. Such list contains information
            //about all node connected to the manager
            register_node(node);
            
            //Update node's preferred connection port for UDP traffic
		    //This information is needed when sent to neighbours so that
		    //they can talk to their neighbours using UDP
		    int port_no = atoi(NODE_PORT) + node->id;
		    int n = sprintf(node->port,"%d",port_no);
		    node->port[n]=0;
            
            //send virtual id and neighbours info
            send_virtual_id_and_neighbours_topo(node->tcp_socketfd, node->id);
		}
		else if (strcmp(token, MESSAGE_ACK) == 0){  //acknoldgement
		    
		    token = strsep (&running, delimiters);
		    
		    if (strcmp(token, MESSAGE_SEND_INFO) == 0){ 
		        //send node virtual id + cost
		        //Also to send full ip + port of node to neighbours
		        //and vice versa for neighbours which are connected.
		        send_node_info_to_neighbours(node->id);
		    }
		    else if (strcmp(token, MESSAGE_INFO_RECEIVED) == 0){  

		        if (setup_completed == 0){
		            //verify everthing is connected. if so notify nodes to start converging.
                    if (check_connections_completed() > 0){
                    
                        setup_completed = 1;
                        sleep(5);
                        
                        send_converge_request();
                    }
                }
		    }
		    else if (strcmp(token, MESSAGE_TOPO_FLUSHED) == 0){
		        
		        pthread_mutex_lock(&mutex_ready_ack);
		        
		        info_received_ack_count++;
		        
		        if (info_received_ack_count == expected_connections){
		            
		            send_converge_request();
		        }
		        
		        pthread_mutex_unlock(&mutex_ready_ack);
		    }
		}
		else if (strcmp(token, MESSAGE_CONVERGED) == 0){
		
		    //send data messages to the node
		    //pthread_mutex_lock(&mutex_converged_ack);
		        
	        //converged_ack_count++;
	        
	        //if (converged_ack_count == expected_connections){
	        
	            send_node_data_messages(node);
	        //}
		        
		    //pthread_mutex_unlock(&mutex_converged_ack);
		}
	}
	
	pthread_exit(NULL);
}

/*
*   Verifies all nodes in the topology file have
*   joined the system.
*/
int check_connections_completed(){
        
    int value = 0;
    
    pthread_mutex_lock(&mutex_node_list);
    
    if(expected_connections == client_nodes->count){
        setup_completed = 1;
        value = 1;
    }else{
        value = -1;
    }
    
    pthread_mutex_unlock(&mutex_node_list);
    
    return value;
}

/*
*
*/
void send_node_data_messages(node_info *node){
    
    //printf("node %d has converged. Sending messages\n", node->id);
    
    char buffer[MAX_BUFFER_SIZE];
    int numchars;
    
    item_link *p = messages->head;
    
    pthread_mutex_lock(&mutex_message_list);
    
    while(p){
        
        bzero(buffer, 0);
        
        data_message *it = (data_message *)p->data;
      
        if (node->id == it->source_id){
           
            numchars = sprintf(buffer, "%s|%d|%d|%s", MESSAGE_DATA, 
                it->source_id, it->dest_id, it->message);                                    
            buffer[numchars] = 0;
            
            //printf("Sending a message ... \n");
            
            send_message(node->tcp_socketfd, buffer);
        }
        
         p = p->next;
    }
    
    pthread_mutex_unlock(&mutex_message_list);
}

/*
*   Notify all nodes of setup completion.
*/
void send_converge_request(){

    //printf("request for converge\n");
    
    item_link * p = client_nodes->head;
    char buffer[256];
    int num_chars = sprintf(buffer, "%s|%s", MESSAGE_START_CONVERGENCE,"go");
    buffer[num_chars] = '\0';
    
    for(p;p!=NULL;p=p->next){  
        node_info *node = (node_info*)p->data;
        if (node){   
            send_message(node->tcp_socketfd, buffer);
        }
    }
}

/*
*   request a new virtual id
*/
void request_virtual_id(int *id){
	pthread_mutex_lock (&mutex_node_id);
    
    *id = next_available_id;
    next_available_id += 1;
    
    pthread_mutex_unlock (&mutex_node_id);
}

/*
*   send virtual neighbours id and cost to a node
*   Format to be sent:  MESSAGE_TOPO_INFO|virtual_id|id1:cost1|id2:cost2|id3:cost3|...;
*/
void send_virtual_id_and_neighbours_topo(int socketfd, int node_id){
    
    char buffer[256];
    char data[256];
    data[0]= '\0';
    strcpy(data,"");

    item_link *p = edges->head;
    
	for(p;p!=NULL;p = p->next){
	    
	    edge *topo_edge = (edge*)p->data;
	    
	    if (topo_edge->u != node_id &&
	        topo_edge->v != node_id )
	        continue;
	       
	    char link_info[10];
	     
	    int id = (topo_edge->u == node_id) ? topo_edge->v: topo_edge->u;
	    
	    sprintf(link_info,"|%d:%d", id, topo_edge->cost);
	    strcat(data, link_info);
	}

    int num_chars = sprintf(buffer, "%s|%d%s", MESSAGE_TOPO_INFO, node_id, data);
    buffer[num_chars] = '\0';
    
    send_message(socketfd, buffer);
}


/*
*
*/
void get_node_neighbours(int node_id, item_list *neighbours){
    
	item_link *p = edges->head;  //edges from the topology file 
	
	for(p;p!=NULL;p = p->next){
	    
	    edge *topo_edge = (edge*)p->data;
	    
	    if (topo_edge->u != node_id &&
	        topo_edge->v != node_id )
	        continue;
	       
	    char link_info[10];
	     
	    int neighbour_id = (topo_edge->u == node_id) ? topo_edge->v: topo_edge->u;
	    
	    add_to_list(neighbours, (void*)neighbour_id);
	}
}

/*
*   Sends new connected node information to already connected nodes.
*/
void send_node_info_to_neighbours(int node_id){
    
    char buffer[MAX_BUFFER_SIZE];
    int num_chars;
    node_info *ninfo = NULL;
    
    item_list *neighbours = malloc(sizeof(*neighbours));
    
    get_node_neighbours(node_id, neighbours);
    
    node_info* current_node = find_node_info_by_id(node_id);
    
    item_link *r = neighbours->head;
    
    //send new node info to connected neighbors.
    for(r; r!=NULL; r=r->next){
	    int neighbour_id = (int)r->data; //neighbours stores a list of node id

        ninfo = find_node_info_by_id(neighbour_id);
        
        if (!ninfo)
            continue;

        send_full_node_information(ninfo->tcp_socketfd, current_node);
        send_full_node_information(current_node->tcp_socketfd, ninfo);
        
        sleep(1);
	}
	
	delete_list(&(neighbours->head));
	
	free(neighbours);
}

/*
*   Sends node information the specified node_info
*   Format to be sent:  MESSAGE_NODE_INFO|virtual_id|host|port;
*/
void send_full_node_information(int socketfd, node_info *ninfo){

    char buffer[MAX_BUFFER_SIZE];
    int num_chars;
    
    bzero(buffer,MAX_BUFFER_SIZE);
    char node_data[MAX_BUFFER_SIZE];
    sprintf(node_data,"%d|%s|%s",ninfo->id, ninfo->host, ninfo->port);
    num_chars = sprintf(buffer, "%s|%s", MESSAGE_NODE_INFO, node_data);
    buffer[num_chars] = '\0';
    
    //printf("sending node full info: %s\n", buffer);
    
    send_message(socketfd, buffer);
}

/*
*   find a node by its virtual id
*/
node_info *find_node_info_by_id(int node_id){
    
    node_info *node = NULL;
    
    pthread_mutex_lock(&mutex_node_list);
    
    item_link * p = client_nodes->head;
    
    for(p;p!=NULL;p=p->next){
        
        if (((node_info*)p->data)->id == node_id){
            node = (node_info*)p->data;
            break;
        }
    }

    pthread_mutex_unlock(&mutex_node_list);
    
    return node;
}

/*
*   reads the topology file and returns a list of edge
*/
void read_topology_file(char* filename){
	int c;
	FILE *file;
	file= fopen(filename,"r");
	char line[MAX_BUFFER_SIZE];
	
	edges = malloc(sizeof(*edges));
	const char delimiters[] = " ";
	char *running, *token;
	
	int num_messages = 0;
	
	if (file){
		while(fgets(line,MAX_BUFFER_SIZE,file)!=NULL){
			
			size_t ln = strlen(line) - 1;
			if (line[ln] == '\n')
			    line[ln] = '\0';
			    
			edge *topo_edge = malloc(sizeof(*topo_edge));
			
			running = strdup(line);
			
			token = strsep(&running, delimiters);
			topo_edge->u = atoi(token);
			
			token = strsep(&running, delimiters);
			topo_edge->v = atoi(token);
			
			token = strsep(&running, delimiters);
			topo_edge->cost = atoi(token);
			
			add_to_list(edges, topo_edge);
			
			int id = (topo_edge->u > topo_edge->v)?topo_edge->u:topo_edge->v;
			
			if (id > expected_connections){
			    expected_connections = id;
			}
			
			num_messages++;
		}
		
	    expected_topo_ack = num_messages*2;
	}

	fclose(file);
}

//reads the provided message file
void read_message_file(char* filename){
	int c;
	FILE *file;
	file= fopen(filename,"r"); 
	char line[MAX_BUFFER_SIZE];
	int numchars;
	
	messages = malloc(sizeof(*messages));
	const char delimiters[] = " ";
	
	if (file){

		while(fgets(line,MAX_BUFFER_SIZE,file)!=NULL){

			size_t ln = strlen(line) - 1;
			if (line[ln] == '\n')
			    line[ln] = '\0';
			
			char *running, *token;
			data_message *n_mess = malloc(sizeof(*n_mess));
			running = strdup(line);
			
			token = strsep(&running, delimiters);
			n_mess->source_id = atoi(token);
			
			token = strsep(&running, delimiters);
			n_mess->dest_id = atoi(token);
			
			n_mess->message = malloc(MAX_BUFFER_SIZE);
 
			strcpy(n_mess->message,running);
			
			//numcharsstrncpy(n_mess->message, running, strlen(running));
			add_to_list(messages, n_mess);
		}
	}

	fclose(file);
}

/*
*   Initializes the list storing node information
*/
void initialize_node_list(){
   client_nodes = malloc(sizeof(*client_nodes));
   client_nodes->head = NULL;
   client_nodes->tail = NULL;
   client_nodes->count = 0;
}

/*
*   adds a node to the list
*/
void register_node(node_info *node){

    pthread_mutex_lock(&mutex_node_list);
    
    add_to_list(client_nodes, (void*)node);

    pthread_mutex_unlock(&mutex_node_list);
}

/*
*   creates a new node info
*/
node_info *create_node_info(char* host, in_port_t port, int socketfd){

    node_info *nd_item = malloc(sizeof(*nd_item));
	nd_item->host = malloc(INET6_ADDRSTRLEN);
	nd_item->host[0] = '\0';
	strcpy(nd_item->host, host);
	
	nd_item->port = malloc(256);
	
    char portno[7];
    sprintf(portno, "%d", port);
    portno[6] = '\0';
    
    strcpy(nd_item->port, portno);
	nd_item->tcp_socketfd = socketfd;
	
	return nd_item;
}

void notify_topo_change(int u, int v, int cost){
    
    char buffer[256];
    int num_chars;
    node_info* u_node = find_node_info_by_id(u);
    node_info* v_node = find_node_info_by_id(v);
    
    //send update to u and v
    num_chars = sprintf(buffer,"%s|%d:%d", MESSAGE_TOPO_UPDATE, v, cost); 
    buffer[num_chars] = '\0';
    send_message(u_node->tcp_socketfd, buffer);
    
    bzero(buffer, 256);
    
    num_chars = sprintf(buffer,"%s|%d:%d", MESSAGE_TOPO_UPDATE, u, cost); 
    buffer[num_chars] = '\0';
    send_message(v_node->tcp_socketfd, buffer);
    
    //force all node to discard routing tables
    item_link *p = client_nodes->head;
    
    bzero(buffer, 256);
    num_chars = sprintf(buffer,"%s|go", MESSAGE_FLUSH_TOPO); 
    
    info_received_ack_count = 0;
    
    sleep(2);
    
    while(p){
        node_info *node = (node_info*)p->data;
        
        //send message
        send_message(node->tcp_socketfd, buffer);
        p = p->next;
    }
    
}

void *std_in_handler(void *pvdata){

    char buffer[MAX_BUFFER_SIZE];
    const char delimiters[] = " ";
    size_t n;
    
    char *running,*token;
    
    while (fgets(buffer, MAX_BUFFER_SIZE, stdin)) {
    
        int ln = strlen(buffer);

        if (buffer[ln] == '\n')
            buffer[ln] = '\0';
            
        //edge *topo_edge = malloc(sizeof(*topo_edge));

        running = strdup(buffer);

        token = strsep(&running, delimiters);
        int u_id = atoi(token);

        token = strsep(&running, delimiters);
        int v_id = atoi(token);

        token = strsep(&running, delimiters);
        int cost = atoi(token);
        
        //send the edge udpate to the client.
        notify_topo_change( u_id, v_id, cost);
    }
}

int main(int argc, char *argv[])
{
	int manager;    //manager socket descriptor
	int node;	    //node
	int numbytes;
	struct addrinfo hints, *managerinfo, *p;
	struct sockaddr_storage node_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv; //return value for getaddrinfo call
	char sequence[5];
	char buffer[100];
    in_port_t accepted_node_port;
    
    pthread_mutex_init(&mutex_node_id, NULL);
    pthread_mutex_init(&mutex_node_list, NULL);
    pthread_mutex_init(&mutex_message_list, NULL);
    pthread_mutex_init(&mutex_ready_ack, NULL);
    pthread_mutex_init(&mutex_converged_ack, NULL);
    
	if (argc != 3) {
	    fprintf(stderr,"usage: manager topologyfile messagefile\n");
	    exit(1);
	}

	read_topology_file(argv[1]);
	read_message_file(argv[2]);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, MANAGER_PORT, &hints, &managerinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = managerinfo; p != NULL; p = p->ai_next) {
		if ((manager = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("manager: socket");
			continue;
		}

		if (setsockopt(manager, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(manager, p->ai_addr, p->ai_addrlen) == -1) {
			close(manager);
			perror("manager: bind");
			continue;
		}

		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "manager: failed to bind\n");
		return 2;
	}

	freeaddrinfo(managerinfo); // all done with this structure

	if (listen(manager, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	//printf("manager: waiting for connections...\n");
	
    initialize_node_list();
    
    //start listening on stdin
    pthread_t std_in_thread;
    pthread_create(&std_in_thread, NULL, (void*)std_in_handler, NULL);

	while(1) {  // main accept() loop

		sin_size = sizeof node_addr;
		node = accept(manager, (struct sockaddr *)&node_addr, &sin_size);

		if (node < 0) {
			perror("Unable to accept connection");
			continue;
		}

		inet_ntop(node_addr.ss_family,
		get_in_addr((struct sockaddr *)&node_addr), s, sizeof s);

		accepted_node_port = ntohs(get_in_port((struct sockaddr *)&node_addr));

		//printf("manager: connected to node [%s:%d]\n", s, accepted_node_port);
		
		node_info *contact_node = create_node_info(s, accepted_node_port, node);

        //start a separate thread for the node connection
		pthread_t thread;
		pthread_create(&thread, NULL, (void*)&thread_handler, (void *)contact_node);
	}
	
	pthread_mutex_destroy(&mutex_node_id);
	pthread_mutex_destroy(&mutex_node_list);
    pthread_mutex_destroy(&mutex_message_list);
    pthread_mutex_destroy(&mutex_ready_ack);
    pthread_mutex_destroy(&mutex_converged_ack);
    
    pthread_exit(NULL);

	return 0;
}


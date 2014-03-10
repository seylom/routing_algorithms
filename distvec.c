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

#include <signal.h>
#include <time.h>

#define MANAGER_PORT "5555"
#define NODE_PORT "6666"
#define BACKLOG 10

#define SIG SIGUSR1

#include <pthread.h>
#include "protocol_helper.h"

int virtual_id = 0;
pthread_mutex_t mutex_routing_list;
timer_t node_timer;

struct node_data *nd_data;
struct item_list *distvect_table;

void start_timer(timer_t timer, int seconds)
{
	struct itimerspec spec;

	spec.it_value.tv_sec = seconds;
	spec.it_value.tv_nsec = 0;
	spec.it_interval.tv_sec = 0;
	spec.it_interval.tv_nsec = 0;

    if (timer_settime(timer, 0, &spec, NULL) == -1) {
		perror("timer_settime");
		exit(1);
	}
}

void time_elapsed(){
    
    printf("node %d has converged\n", nd_data->node->id);
    //send message to the manager to get messages to be sent.
}

void setup_timer(){ 

	struct sigaction action;
	struct sigevent event;

    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = time_elapsed;
    sigemptyset(&action.sa_mask);
    sigaction(SIG, &action, NULL);

    event.sigev_notify = SIGEV_SIGNAL;
    event.sigev_signo = SIG;
    event.sigev_value.sival_ptr = &node_timer;
    timer_create(CLOCK_REALTIME, &event, &node_timer);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
*   find a node by its virtual id
*/
void find_neighbour_info(int node_id, item_list *list, node_info **node){
    
    if (!list)
        return NULL;
        
    item_link * p = list->head;
    
    for(p;p!=NULL;p=p->next){
        
        if (((node_info*)p->data)->id == node_id){
            *node = (node_info*)p->data;
            break;
        }
    }
}

void find_table_entry(int destination_id, distvec_entry **entry){
    
    item_link *p = distvect_table->head;
    distvec_entry *current = NULL;
    
    while(p){
    
        current = (distvec_entry*)p->data;
        
        if (current->destination_id == destination_id){
            *entry = current;
            return;
        }
    
        p = p->next;
    }
    
    *entry = NULL;
}

int cost_to_hop(int neighbour_id){
    
    item_link *p = nd_data->neighbours->head;
    neighbour *current;
    
    while(p){
        current = (neighbour*)p->data;
        
        if (current->id == neighbour_id)
            return current->cost;
    }
    
    return -1; //not found
}

void add_routing_table_entry(int destination_id, int next_hop_id, int cost){
    
    distvec_entry *entry = (distvec_entry*)malloc(sizeof(distvec_entry));
    entry->destination_id = destination_id;
    entry->cost = cost;
    entry->next_hop = next_hop_id;
    
    printf("adding entry - %d %d %d\n",entry->destination_id, entry->next_hop, entry->cost);
    
    add_to_list(distvect_table, entry);
}

int update_routing_table_entries(item_list *new_entries){
    int update = 0;
    item_link *p = new_entries->head;
    
    while(p){
        distvec_entry *entry = (distvec_entry*)p->data;
        
        if (update_routing_table_entry(entry->destination_id, entry->next_hop, entry->cost) > 0) 
            update = 1;
    }
    
    return update;
}


int update_routing_table_entry(int destination_id, int next_hop_id, int cost){
    
    int update = 0;
    pthread_mutex_lock(&mutex_routing_list); 
    //check and update if necessary
    
    distvec_entry *entry;
    find_table_entry(destination_id, &entry);
    
    if (!entry){     
        //add if the entry was never seen before
        add_routing_table_entry(destination_id, next_hop_id, cost);
        update = 1;
    }
    else{
        //compare cost and udpate
        int hop_cost = cost_to_hop(next_hop_id);
           
        if (entry->cost > (cost + hop_cost)){
            entry->cost = cost + hop_cost;
            entry->next_hop = next_hop_id;
            update = 1;
        } 
    }
    
    pthread_mutex_unlock(&mutex_routing_list);
    
    return update; //no updates
}

void get_routing_table_broadcast(int node_id, char **message){
    
    //char buffer[256];
    char data[256];
    
    data[0]= '\0';
    strcpy(data,"");
    
    pthread_mutex_lock(&mutex_routing_list);

    item_link *p = distvect_table->head;
    
	for(p;p!=NULL;p = p->next){    
	    distvec_entry *entry = (distvec_entry*)p->data;
	    char link_info[10];

	    sprintf(link_info,"|%d:%d:%d", entry->destination_id, entry->next_hop, entry->cost);  
	    strcat(data, link_info);
	}
	
	pthread_mutex_unlock(&mutex_routing_list);

    int num_chars = sprintf(message, "%s|%d%s", MESSAGE_DISTVEC, node_id, data);
    (*message)[num_chars] = '\0';
}

// start convergence process.
void build_network_map(){

    char buffer[256];
    bzero(buffer, 0);
    
    if(!nd_data){
        return;
    }
    
    node_info *node = nd_data->node;
    
    if (node)
        printf("Starting convergence process for node %d\n", node->id);
    
    //send node distance vector view to neighbours.
    
    item_list *neighbours = nd_data->neighbours;
    item_list *costs = nd_data->neighbours_cost;
    
    item_link *p = costs->head;
    
    //add the current node to the list
    update_routing_table_entry(node->id, node->id, 0);
    
    for(p;p!=NULL;p=p->next){
        neighbour *nb = (void*)p->data;
        
        node_info *nb_node;
        find_neighbour_info(nb->id,neighbours, &nb_node);
        
        if (!nb_node)
            continue;

        update_routing_table_entry(nb->id, nb->id, nb->cost);
    }
    
/*    //create broadcast message for neighbors*/
/*    get_routing_table_broadcast(node->id, &buffer);*/
/*    */
/*    item_link *q = neighbours->head;*/
/*    */
/*    for(q;q!=NULL;q=q->next){*/
/*        node_info *ninfo = (void*)q->data;*/
/*        */
/*        if (ninfo)*/
/*            send_udp_message(ninfo->host,ninfo->port, buffer);*/
/*    }*/

    broadcast_routing_table();
}

void broadcast_routing_table(){
    char buffer[256];
    char data[256];
    
    int node_id = nd_data->node->id;
    
    bzero(buffer, 0);
    data[0]= '\0';
    bzero(buffer, 0);
    strcpy(data,"");
    
    //creating message to be sent to neighbors
    pthread_mutex_lock(&mutex_routing_list);

    item_link *p = distvect_table->head;
    
	for(p;p!=NULL;p = p->next){    
	    distvec_entry *entry = (distvec_entry*)p->data;
	    char link_info[10];

	    sprintf(link_info,"|%d:%d:%d", entry->destination_id, entry->next_hop, entry->cost);  
	    strcat(data, link_info);
	}
	
	pthread_mutex_unlock(&mutex_routing_list);

    int num_chars = sprintf(buffer, "%s|%d%s", MESSAGE_DISTVEC, node_id, data);
    buffer[num_chars] = '\0';
    
    //send message to all neighbours
    broadcast_message(buffer);
    
    start_timer(node_timer, 5);
}


void broadcast_message(char* message){
    //sending message to neighbours
    item_link *q = nd_data->neighbours->head;
    
    for(q;q!=NULL;q=q->next){
        node_info *ninfo = (void*)q->data;
        
        if (ninfo)
            send_udp_message(ninfo->host,ninfo->port, message);
    }
}

/*
*   Extract destination:next_hop:cost from message
*/
neighbour *extract_hop_info(char *message){
	if (message == NULL)
		return NULL;
	
	const char info_delimiters[] = ":";
	
	printf("%s\n", message);
	
	distvec_entry *entry = malloc(sizeof(distvec_entry));
	
	char *token, *running;
	running = strdup(message);
	
	token = strsep(&running, info_delimiters);
	entry->destination_id = atoi(token);
		
	token = strsep(&running, info_delimiters);
	entry->next_hop = atoi(token);
	
	token = strsep(&running, info_delimiters);
	entry->cost = atoi(token);

	return entry;
}

/*
*   extract distvec from message
*/
item_list *extract_distvec_list(char *message){
	
	if (message == NULL)
		return NULL;
		
	const char array_delimiters[] = "|";
	item_list *list = malloc(sizeof(item_list));
	
	char *token, *running;
	running = strdup(message);
	token = strsep(&running, array_delimiters);
	
	while(token){
		distvec_entry *entry = extract_hop_info(token);
		
		if (entry){
			add_to_list(list, (void*)entry);
		}else{
		    break;
		}
		
		token = strsep(&running, array_delimiters);
	}
	
	return list;
}

/*
*   handles UDP packet messages
*/
void *udp_handler_distvec(void* pvdata){
    udp_message *mess_info = (udp_message*)pvdata; 
    const char delimiters[] = "|";
    
    char *token,*running;
    running = strdup(mess_info->message);
    
    token = strsep(&running, delimiters);
    
    //update_table_entries
    if(strcmp(token, MESSAGE_DISTVEC) == 0){  
    
        token = strsep(&running, delimiters);
        
        int source_id = atoi(token);
        item_list *dv_list = extract_distvec_list(running);
        
        if (update_routing_table_entries(dv_list)){ 
            //braodcast update to neighbours
            broadcast_routing_table();
            //reset timer and wait.        
        }
    }
}

void initialize_list(item_list *list){

}

void initialize_data_container(node_data *ndata){
    nd_data = malloc(sizeof(node_data));
	
	nd_data->neighbours = malloc(sizeof(item_list));
	nd_data->neighbours->head = NULL;
	nd_data->neighbours->tail = NULL;
    nd_data->neighbours->count = 0;
    
    nd_data->neighbours_cost = malloc(sizeof(item_list));
	nd_data->neighbours_cost->head = NULL;
	nd_data->neighbours_cost->tail = NULL;
    nd_data->neighbours_cost->count = 0;
    
    nd_data->protocol_handler = build_network_map;
    nd_data->udp_handler = udp_handler_distvec;
    
    distvect_table = malloc(sizeof(item_list));
    distvect_table->head = NULL;
    distvect_table->tail = NULL;
    distvect_table->count = 0;
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
	
	pthread_mutex_init(&mutex_routing_list, NULL);
	
	setup_timer();
	
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
	
	strcpy(item->host, "localhost");
	
	//create a thread to handle communication with manager
	pthread_t thread;
	pthread_create(&thread, NULL, (void*)node_to_manager_handler, (void*)nd_data);

	while(1){
        //actually, there is nothing here since the thread handling TCP connections
        //waits on virtual id assignment to kick off UDP port reading (port_no = virtual_id + 7000)
	}

	close(node_udp);
    close(node_tcp);
    
    pthread_mutex_destroy(&mutex_routing_list);
    
	return 0;
}


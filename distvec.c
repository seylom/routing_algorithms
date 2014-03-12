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
int volatile ready = 0;
pthread_mutex_t mutex_routing_list;
pthread_mutex_t mutex_routing_update;
timer_t node_timer;

struct node_data *nd_data;
struct item_list *distvect_table;
typedef int (*compfn)(const void*, const void*);
void build_network_map();

data_message  *extract_message(char *message, int from_neighbour);
item_list *extract_distvec_list(char *message);
distvec_entry *extract_hop_info(char *message);
void find_table_entry(int dest_id, distvec_entry **entry);
void start_timer(timer_t timer, int seconds);
void time_elapsed();
void time_elapsed();
int cost_to_hop(int neighbour_id);
void get_routing_table_broadcast(int node_id, char **message);

static int compare(const void *p1, const void *p2)
{
    int y1 = ((const struct distvec_entry*)p1)->dest_id;
    int y2 = ((const struct distvec_entry*)p2)->dest_id;

    if (y1 < y2)
        return -1;
    else if (y1 > y2)
        return 1;

    return 0;
}

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
    
    //printf("node %d has converged\n", nd_data->node->id);
    
    //send message to the manager to get messages to be sent, if any
    char buffer[MAX_DATA_SIZE];
    int num_chars = sprintf(buffer, "%s|%s", MESSAGE_CONVERGED, MESSAGE_SEND_DATA);
    buffer[num_chars] = '\0';
    
    print_routing_table();
    
    send_message(nd_data->node->tcp_socketfd, buffer);
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

void find_table_entry(int dest_id, distvec_entry **entry){
    
    item_link *p = distvect_table->head;
    distvec_entry *current = NULL;
    
    while(p){
    
        current = (distvec_entry*)p->data;
        
        if (current->dest_id == dest_id){
            *entry = current;
            return;
        }
    
        p = p->next;
    }
    
    *entry = NULL;
}

int cost_to_hop(int neighbour_id){
    
    item_link *p = nd_data->neighbours_cost->head;
    neighbour *current;
    
    while(p){
        current = (neighbour*)p->data;
        
        if (current->id == neighbour_id){
            return current->cost;
        }
            
        p = p->next;
    }
    
    return -1; //not found
}

void get_routing_table_broadcast(int node_id, char **message){
    
    //char buffer[256];
    char data[MAX_DATA_SIZE];
    
    data[0]= '\0';
    strcpy(data,"");
    
    pthread_mutex_lock(&mutex_routing_list);

    item_link *p = distvect_table->head;
    
	for(p;p!=NULL;p = p->next){    
	    distvec_entry *entry = (distvec_entry*)p->data;
	    char link_info[10];

	    sprintf(link_info,"|%d:%d:%d", entry->dest_id, entry->next_hop, entry->cost);  
	    strcat(data, link_info);
	}
	
	pthread_mutex_unlock(&mutex_routing_list);

    int num_chars = sprintf(message, "%s|%d%s", MESSAGE_DISTVEC, node_id, data);
    (*message)[num_chars] = '\0';
}

// start convergence process.
void build_network_map(){

    char buffer[MAX_DATA_SIZE];
    bzero(buffer, MAX_DATA_SIZE);
    
    if(!nd_data){
        return;
    }
    
    node_info *node = nd_data->node;
    
/*    if (node)*/
/*        printf("Starting convergence process for node %d\n", node->id);*/
    
    //send node distance vector view to neighbours.
    pthread_mutex_lock(&mutex_routing_update);
    
    item_list *neighbours = nd_data->neighbours;
    item_list *costs = nd_data->neighbours_cost;
    
    item_link *p = costs->head;
    
    //add the current node to the list
    //update_routing_table_entry(node->id, node->id, node->id, 0);
    add_routing_table_entry(node->id, node->id, 0);
    
    for(p;p!=NULL;p=p->next){
        neighbour *nb = (void*)p->data;
        
        node_info *nb_node;
        find_neighbour_info(nb->id,neighbours, &nb_node);
        
        if (!nb_node)
            continue;

        //printf("adding %d->%d->%d to table", nb->id, nb->id, nb->cost);
        //update_routing_table_entry(node->id, nb->id, nb->id, nb->cost);
        add_routing_table_entry(nb->id, nb->id, nb->cost);
    }
    
    pthread_mutex_unlock(&mutex_routing_update);

    broadcast_routing_table();
    
    ready = 1;
}

void broadcast_routing_table(){
    char buffer[MAX_DATA_SIZE];
    char data[MAX_DATA_SIZE];
    
    int node_id = nd_data->node->id;
    
    bzero(buffer, MAX_DATA_SIZE);
    data[0]= '\0';
    bzero(buffer, MAX_DATA_SIZE);
    strcpy(data,"");
    
    //creating message to be sent to neighbors
    pthread_mutex_lock(&mutex_routing_list);

    //printf("size of routing table: %d\n", distvect_table->count);
    
    item_link *p = distvect_table->head;
    
	for(p;p!=NULL;p = p->next){    
	    distvec_entry *entry = (distvec_entry*)p->data;
	    char link_info[10];

	    sprintf(link_info,"|%d:%d:%d", entry->dest_id, entry->next_hop, entry->cost);  
	    strcat(data, link_info);
	}
	
	pthread_mutex_unlock(&mutex_routing_list);

    int num_chars = sprintf(buffer, "%s|%d%s", MESSAGE_DISTVEC, node_id, data);
    buffer[num_chars] = '\0';
    
    //send message to all neighbours
    broadcast_message(buffer);
    
    start_timer(node_timer, 5);
}

void print_routing_table(){

    struct distvec_entry entries[distvect_table->count];
    
    item_link *p = distvect_table->head;
    int i = 0;
    int j = 0;
    
    for(p; p!=NULL; p=p->next){
        distvec_entry *item = (distvec_entry*)p->data;
        
        entries[i] = *item;
        i++;
    }
    
    qsort(&entries, distvect_table->count, 
            sizeof(struct distvec_entry), 
            (compfn) compare);
    
    printf("\n");
           
    for(j=0; j< distvect_table->count; j++){
        printf("%d %d %d\n", entries[j].dest_id,  entries[j].next_hop,  entries[j].cost);
    }
    
    printf("\n");
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
*   handles UDP packet messages
*/
void *udp_handler_distvec(void* pvdata){

    while(!ready){}

    udp_message *mess_info = (udp_message*)pvdata; 
    const char delimiters[] = "|";

    char *token,*running;
    running = strdup(mess_info->message);
    
    token = strsep(&running, delimiters);
    
    //update_table_entries
    if(strcmp(token, MESSAGE_DISTVEC) == 0){  
    
        token = strsep(&running, delimiters);
        
        int owner_id = atoi(token);
        item_list *dv_list = extract_distvec_list(running);

        if (update_routing_table_entries(owner_id, dv_list)){ 
            //braodcast update to neighbours
            broadcast_routing_table();
            //reset timer and wait.        
        }
        
        //should free stuff here!
    }
    else if(strcmp(token, MESSAGE_DELIVER) == 0){ 
        //process the message
        distvec_message_router(running, 1);
    }
}


int update_routing_table_entries(int owner_id, item_list *new_entries){

    pthread_mutex_lock(&mutex_routing_update);
    
    int update = 0;
    
    item_link *p = new_entries->head;
 
    while(p){
        distvec_entry *entry = (distvec_entry*)p->data;
            
        if (update_routing_table_entry(owner_id, entry->dest_id, entry->next_hop, entry->cost) > 0) 
            update = 1;
            
        p = p->next;
    }
    
    pthread_mutex_unlock(&mutex_routing_update);
    
    return update;
}

int update_routing_table_entry(int owner_id, int dest_id, int next_hop_id, int cost){
    
    int update = 0;
    
    pthread_mutex_lock(&mutex_routing_list); 
    //check and update if necessary
    
    distvec_entry *entry;
    
    find_table_entry(dest_id, &entry);
    
    int hop_cost = cost_to_hop(owner_id);
    
    if (!entry){
        //add if the entry was never seen before
        add_routing_table_entry(dest_id, owner_id, hop_cost + cost);
        update = 1;
    }
    else{
        //compare cost and udpate
         
        int hop_cost = cost_to_hop(owner_id);
        
        if (entry->cost > (cost + hop_cost)){
            entry->cost = cost + hop_cost;
            entry->next_hop = owner_id;
            update = 1;
        } 
    }
    
    pthread_mutex_unlock(&mutex_routing_list);
    
    return update; //no updates
}

void add_routing_table_entry(int dest_id, int next_hop_id, int cost){
    
    distvec_entry *entry = malloc(sizeof(*entry));
    entry->dest_id = dest_id;
    entry->cost = cost;
    entry->next_hop = next_hop_id;

    add_to_list(distvect_table, entry);
}

/*
*   extract distvec from message
*/
item_list *extract_distvec_list(char *message){
	
	if (message == NULL)
		return NULL;
		
	const char array_delimiters[] = "|";
	item_list *list = malloc(sizeof(*list));
	
	char *token, *running;
	running = strdup(message);
	token = strsep(&running, array_delimiters);
	
	while(token){
		distvec_entry *entry = extract_hop_info(token);
		
		if (entry){
			add_to_list(list, entry);
		}else{
		    break;
		}
		
		token = strsep(&running, array_delimiters);
	}
	
	return list;
}

/*
*   Extract destination:next_hop:cost from message
*/
distvec_entry *extract_hop_info(char *message){
	if (message == NULL)
		return NULL;
	
	const char info_delimiters[] = ":";
	
	distvec_entry *entry = malloc(sizeof(*entry));
	
	char *token, *running;
	running = strdup(message);
	
	token = strsep(&running, info_delimiters);
	entry->dest_id = atoi(token);
		
	token = strsep(&running, info_delimiters);
	entry->next_hop = atoi(token);
	
	token = strsep(&running, info_delimiters);
	entry->cost = atoi(token);

	return entry;
}


void initialize_list(item_list *list){

}


void send_data_message(data_message *mess_info){

    char buffer[MAX_DATA_SIZE];
    bzero(buffer, MAX_DATA_SIZE);
    int num_chars;
    
    //find routing entry.
    item_link *p = distvect_table->head;
    distvec_entry *entry,*current;
    entry = NULL;
    
    while(p){
        current = (distvec_entry*)p->data;
        
        if (current->dest_id == mess_info->dest_id){
            entry = current;
            break;
        }
        
        p = p->next;
    }

    if (entry){
        node_info *nb;
        
        find_neighbour_info(entry->next_hop,nd_data->neighbours, &nb);
        
        if(!nb){
            //printf("Error! neighbour not ready!");
            return;
        }
        
        if (mess_info->source_id != nd_data->node->id){
            num_chars = sprintf(buffer, "%s|%d|%d|%s %d|%s", MESSAGE_DELIVER,
                        mess_info->source_id, mess_info->dest_id, mess_info->path,
                        nd_data->node->id, mess_info->message);
        }
        else{
            num_chars = sprintf(buffer, "%s|%d|%d|%d|%s", MESSAGE_DELIVER,
                        mess_info->source_id, mess_info->dest_id,
                        nd_data->node->id, mess_info->message);
        }
                       
        buffer[num_chars] = 0;

        send_udp_message(nb->host,nb->port, buffer);
    }
}

void distvec_message_router(char *message, int from_neighbour){
    
   data_message *mess_info = extract_message(message, from_neighbour);
         
    //extract message and send to next hop

    if (from_neighbour){
        printf("from %d to %d hops %s %d message %s\n", 
                        mess_info->source_id,
                        mess_info->dest_id,
                        mess_info->path,
                        nd_data->node->id,
                        mess_info->message);
    }
    else{
        printf("from %d to %d hops %d message %s\n", 
                        mess_info->source_id,
                        mess_info->dest_id,
                        nd_data->node->id,
                        mess_info->message);
    
    }
                        
    if (mess_info->dest_id == nd_data->node->id){    
        return;
    }
    
    send_data_message(mess_info); 
}

void notify_topology_change(){
    
    ready = 0;
    
    //flush the routing table
    //delete_list(distvect_table);
    
    distvect_table = malloc(sizeof(*distvect_table));
    distvect_table->head = NULL;
    distvect_table->tail = NULL;
    distvect_table->count = 0;
    
    //distvect_table->count = 0;
}


/*
*   Initializes local data store for node information
*/
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
    nd_data->route_message_handler = distvec_message_router;
    nd_data->topology_change_handler = notify_topology_change;
    
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
	pthread_mutex_init(&mutex_routing_update, NULL);
	
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


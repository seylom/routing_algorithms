

#ifndef _PROTOCOL_HELPER_H
#define _PROTOCOL_HELPER_H

#define MANAGER_PORT "5555"
#define NODE_PORT "7000"

#define MESSAGE_CONTACT "contact"
#define MESSAGE_TOPO_INFO "topo_info"
#define MESSAGE_NODE_INFO "node_info"
#define MESSAGE_SEND_INFO "send_info"
#define MESSAGE_SEND_DATA "send_data"
#define MESSAGE_ACK "ack"
#define MESSAGE_INFO_RECEIVED "info_received"
#define MESSAGE_START_CONVERGENCE "start"
#define MESSAGE_DATA "data"
#define MESSAGE_DISTVEC "distvec_broadcast"
#define MESSAGE_CONVERGED "converged"
#define MESSAGE_DELIVER "deliver_message"
#define MESSAGE_TOPO_UPDATE "topo_update"
#define MESSAGE_FLUSH_TOPO "flush_topo"
#define MESSAGE_TOPO_FLUSHED  "topo_flushed"
#define MESSAGE_TOPO_UPDATED "topo_updated"
#define MAX_DATA_SIZE 1024
#define MAX_BUFFER_SIZE 1024

typedef struct edge{
    int u;
    int v;
    int cost;
}edge;

typedef struct node_info{
    int id;
    char* host;
    char* port;
    int tcp_socketfd;
    int udp_socketfd;
}node_info;

typedef struct item_list{
    struct item_link *head, *tail;
    int count;
}item_list;

typedef struct item_link{
    void *data;  
    struct item_link *next;
}item_link;

typedef struct neighbour{
	int id;
	int cost;
}neighbour;

typedef struct udp_message{
    char *message;
    node_info *source;
}udp_message;

typedef struct node_data{
    node_info *node;
    item_list *neighbours;
    item_list *neighbours_cost;
    item_list *messages;
    void(*protocol_handler)();
    void(**udp_handler)(void*);
    void(*route_message_handler)(char*);
    void(*topology_change_handler)();
}node_data;

typedef struct distvec_entry{
    int dest_id;
    int next_hop;
    int cost;
}distvec_entry;

typedef struct data_message{
    int source_id;
    int dest_id;
    char* message;
    char* path;
}data_message;

void send_converge_request();
void send_message(int socketfd, char* message);
int setup_tcp_connection(char* host, char* port);
int setup_udp_connection(char* host, char* port);
void *node_to_manager_handler(void* pvnode_item);
void send_udp_message(char *host, char *port, char *message);

void send_node_info_to_neighbours(int id);
node_info *find_node_info_by_id(int node_id);
void request_virtual_id(int *id);
item_list *extract_neighbors_info(char *message);
neighbour *extract_neighbor(char *message);
node_info *extract_node_information(char *message);


#endif

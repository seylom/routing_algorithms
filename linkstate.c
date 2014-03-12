/*
 ============================================================================
 Name        : linkstate.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : MP2 List State Node Program
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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
#include <limits.h>
#include "protocol_helper.h"

#define GRAPH_SIZE 17

void initialize_data_container(node_data *ndata);
void build_network_map();
void *udp_handler_linkstate(void* pvdata);
int find_minimum_node(int shortest[], int visited[]);
int find_next_hop(int node_id);


int topology[GRAPH_SIZE][GRAPH_SIZE];

// Previous node in shortest path
int pred[GRAPH_SIZE];

// shortest path cost for each node
int shortest[GRAPH_SIZE];

int visited[GRAPH_SIZE];

int forwarding_table[GRAPH_SIZE];

int lsp_counter = 0;

struct LSP {
	int node_id;
	int neighbors[GRAPH_SIZE];
	int costs[GRAPH_SIZE];
	int TTL;
};

struct node_data *nd_data;
int node_tcp, node_udp;
int numbytes, filelen;
char buf[MAX_DATA_SIZE];
struct sockaddr_storage node_addr;
socklen_t addr_len;
struct addrinfo manager_hints, node_hints, *managerinfo, *nodeinfo, *p, *q;
int rv;
int yes = 1;
char s[INET6_ADDRSTRLEN];

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: ./link_state <managerhostname>\n");
		exit(1);
	}

	bzero(topology, 16 * 16);

	node_tcp = setup_tcp_connection(argv[1], MANAGER_PORT);
	initialize_data_container(nd_data);

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
	pthread_create(&thread, NULL, (void*) node_to_manager_handler,
			(void*) nd_data);

	while(1){
		sleep(5);
		if (lsp_counter == 5) {
			int i;
			calc_shortest_path();
			//printf("Shortest Path from Node %d\n", nd_data->node->id);
//			for(i=0; i<GRAPH_SIZE; i++) {
//				if (shortest[i] < INT32_MAX)
//					printf("%d : %d  ", i , shortest[i] );
//			}
			printf("\nForwarding table for node %d\n", nd_data->node->id);
			printf("| Node | Next Hop |\n");
			for(i=0; i<GRAPH_SIZE; i++) {
				if (forwarding_table[i] > 0)
					printf("|  %d   |    %d     |\n", i , forwarding_table[i] );
			}
		}
		//display_graph();
	}
	close(node_udp);
	close(node_tcp);

	return 0;
}

void initialize_data_container(node_data *ndata) {
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
	nd_data->udp_handler = udp_handler_linkstate;
}

void build_network_map() {
	printf("Build Network started ...\n");
	while (lsp_counter < 5) {
		sleep(5);
		node_info* item = nd_data->node;
		item_link* head_neighbor_costs = nd_data->neighbours_cost->head;
		item_link* head__neghobor_locations = nd_data->neighbours->head;
		struct LSP lsp;
		char message[256];
		bzero(lsp.costs, GRAPH_SIZE * sizeof(int));
		bzero(lsp.neighbors, GRAPH_SIZE * sizeof(int));
		lsp.node_id = item->id;
		lsp.TTL = 4;
		// building the LSP from neighbors
		while (head_neighbor_costs != NULL) {
			neighbour* neighbor = (neighbour*) head_neighbor_costs->data;
			lsp.neighbors[neighbor->id] = 1;
			lsp.costs[neighbor->id] = neighbor->cost;
			topology[item->id][neighbor->id] = neighbor->cost;
			topology[neighbor->id][item->id] = neighbor->cost;
			head_neighbor_costs = head_neighbor_costs->next;
		}
		// sending LSP to all neighbors
		bzero(message, 256);
		serialize_lsp(message, lsp);
		while (head__neghobor_locations != NULL) {
			node_info* loc = (node_info*) head__neghobor_locations->data;
			send_udp_message(loc->host, loc->port, message);
			//printf("LSP message send to %d : %s\n", loc->id, message);
			head__neghobor_locations = head__neghobor_locations->next;
		}
		lsp_counter++;
		//display_graph();
	}
}

void calc_shortest_path(){
	int i,j,v;
	for(i=0; i< GRAPH_SIZE; i++) {
		pred[i] = 0;
		shortest[i] = INT32_MAX;
		visited[i] = 0;
		forwarding_table[i] = 0;
	}
	int node_id = nd_data->node->id;
	shortest[node_id] = 0;
	pred[node_id] = 0;
	for(i=0; i < GRAPH_SIZE ; i++) {
		if (topology[node_id][i] > 0) {
			shortest[i] = topology[node_id][i];
			pred[i] = node_id;
		}
	}
	while(1) {
		int w = find_minimum_node(shortest, visited);
		if (w < 0) break;

		visited[w] = 1;

		for(v=0; v<GRAPH_SIZE ; v++) {
			if (visited[v]==0 && topology[w][v] > 0) {
				if (shortest[w] + topology[w][v] < shortest[v]) {
					shortest[v] = shortest[w] + topology[w][v];
					pred[v] = w;
				}
			}
		}
	}
	//printf("Shortest path calculation completed\n");

	for(i=0; i<GRAPH_SIZE; i++) {
		if (shortest[i] < INT32_MAX && i!=node_id && shortest[i] > 0)
			forwarding_table[i] = find_next_hop(i);
	}
}

int find_minimum_node(int shortest[], int visited[]) {
	int min = INT32_MAX;
	int min_index = -1;
	int i;
	for(i=0; i <GRAPH_SIZE; i++) {
		if (!visited[i]) {
			if (shortest[i] < min) {
				min = shortest[i];
				min_index = i;
			}
		}
	}
	return min_index;
}

int find_next_hop(int node_id) {
	int source = nd_data->node->id;
	int pred_node = pred[node_id], current_node = node_id;
	while(pred_node != source) {
		//printf("current: %d, prev: %d\n", current_node, pred_node);
		current_node = pred_node;
		pred_node = pred[pred_node];
	}
	return current_node;
}

void *udp_handler_linkstate(void* pvdata) {
	udp_message *mess_info = (udp_message*) pvdata;
	handle_lsp(mess_info->message);
}

void serialize_lsp(char* str, struct LSP lsp) {
	char buf[3];
	int i;
	// format : lsp|node_id|TTL|neighbors' costs
	// packet header
	strcat(str, "lsp");

	// node id
	sprintf(buf, "%d", lsp.node_id);
	strcat(str, "|");
	strcat(str, buf);

	// TTL
	sprintf(buf, "%d", lsp.TTL);
	strcat(str, "|");
	strcat(str, buf);
	for (i = 0; i < GRAPH_SIZE; i++) {
		sprintf(buf, "%d", lsp.costs[i]);
		strcat(str, "|");
		strcat(str, buf);
	}
}

struct LSP deserialize_lsp(char* str) {
	struct LSP lsp;
	char * pch;
	int i;
	pch = strtok(str, "|");
	if (strcmp(pch, "lsp") == 0) {
		pch = strtok(NULL, "|");
		lsp.node_id = atoi(pch);

		pch = strtok(NULL, "|");
		lsp.TTL = atoi(pch);
		for (i = 0; i < GRAPH_SIZE; i++) {
			pch = strtok(NULL, "|");
			lsp.costs[i] = atoi(pch);
			if (lsp.costs[i] > 0)
				lsp.neighbors[i] = 1;
		}
	}

	return lsp;
}

//void display_graph() {
//	int i, j;
//	printf("\nGraph of topology for node %d\n", nd_data->node->id);
//	for (i = 0; i < GRAPH_SIZE; i++) {
//		for (j = 0; j < GRAPH_SIZE; j++) {
//			printf("%d", topology[i][j]);
//		}
//		printf("\n");
//	}
//	printf("------\n");
//}

/* Handles receiving parsing and forwarding LSP packages */
void handle_lsp(char* message) {
	int i;
	struct LSP lsp = deserialize_lsp(message);

	/* own lsp and lsp with zero TTL are invalid */
	if (lsp.node_id != nd_data->node->id && lsp.TTL > 0) {
		int node_id = lsp.node_id;

		// Update topology from received lsp
		for (i = 0; i < GRAPH_SIZE; i++) {
			topology[i][node_id] = lsp.costs[i];
			topology[node_id][i] = lsp.costs[i];
		}
		// forward lsp
		lsp.TTL--;
		char message[256];
		bzero(message, 256);
		serialize_lsp(message, lsp);
		item_link* head_neighbor_locations = nd_data->neighbours->head;
		while (head_neighbor_locations != NULL) {
			node_info* loc = (node_info*) head_neighbor_locations->data;
			//printf("forwarding to node %d (%s:%s)\n", loc->id, loc->host , loc->port);
			if (loc != NULL && loc->id != lsp.node_id) {
				send_udp_message(loc->host, loc->port, message);
				sleep(1);
			}
			head_neighbor_locations = head_neighbor_locations->next;
		}
	}
}


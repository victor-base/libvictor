#ifndef _GRAPH_H
#define _GRAPH_H 1

#include "vector.h"
#include "panic.h"
#include "mem.h"

typedef struct graph_node {
    Vector *vector;
	int idegree;
    int odegree;  
	int odegree_max;

	int alive;
    struct graph_node *neighbors[];
} GraphNode;


#define GRAPH_NODESZ(M) sizeof(GraphNode) + (sizeof(GraphNode *) * (M))

GraphNode *new_gnode(uint64_t id, float32_t *vector, uint16_t dims, int odegree_max) {
	GraphNode *node = (GraphNode *) calloc_mem(1, GRAPH_NODESZ(odegree_max));

	if (node == NULL)
		return NULL;

	node->vector = make_vector(id, vector, dims);
	if (node->vector == NULL) {
		free_mem(node);
		return NULL;
	}	

	node->odegree_max = odegree_max;
	return node;
}

void lazy_delete_gnode(GraphNode *gnode) {
	PANIC_IF(gnode == NULL, "lazy delete of null gnode");
	gnode->alive = 0;
}


#endif
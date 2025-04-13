#ifndef _GRAPH_H
#define _GRAPH_H 1


#include <stdint.h>

#include "vector.h"
#include "panic.h"
#include "heap.h"
#include "method.h"
#include "map.h"
#include "mem.h"

/*
* compute_softlimit - Dynamic softlimit lookup for NSW/HNSW graph construction
*
* This module provides a fast and portable way to compute the softlimit value used during vector graph insertion.
* The goal of the softlimit is to determine how many outgoing connections (neighbors) a new node should create.
* Instead of using a fixed M value (maximum neighbors), this implementation grows the number of neighbors progressively.
*
* --------------------------
* Rationale for softlimit:
* --------------------------
* - A high fixed M leads to excessive and low-quality connections when the graph is small.
* - A dynamic softlimit improves connection quality, memory usage, and scalability.
*
* --------------------------
* Logic behind the table:
* --------------------------
* The softlimit value is computed based on N (number of nodes in the graph) as follows:
*
* 1. For N < 2^16 (65536): softlimit = floor(log2(N))
*    - Motivated by small-world theory: O(log N) neighbors are sufficient for good navigability.
*
* 2. For N >= 65536: softlimit = floor(N^0.25)
*    - This provides a more aggressive growth once the graph reaches a mature size.
*    - Ensures denser connectivity for better recall without excessive cost.
*
* 3. All values are capped by M = 64 (the hard upper limit).
*
* --------------------------
* Implementation:
* --------------------------
* The logic is encoded into a static lookup table for efficiency.
* The index into the table is calculated using the most significant bit (MSB) position of N:
*
*     floor(log2(N)) = 63 - clz(N)  // where clz = count leading zeros
*     index = floor(log2(N)) - 4    // since our table starts at N = 2^4 = 16
*
* The implementation is portable across GCC, Clang, and MSVC using conditional macros.
*/
#define HARDLIMIT_M 64

static const uint8_t softlimit_table[] = {
    4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
    14, 15, 16, 19, 22, 26, 32, 38, 45, 53, 64, 64
};

// Portable MSB detection
static inline int get_msb_index(uint64_t x) {
#if defined(_MSC_VER)
    unsigned long index;
    _BitScanReverse64(&index, x);
    return (int)index;
#elif defined(__GNUC__) || defined(__clang__)
    return 63 - __builtin_clzll(x);
#else
    // Fallback portable implementation
    int i = 0;
    while (x >>= 1) i++;
    return i;
#endif
}

static inline int compute_softlimit(uint64_t N) {
    if (N < 16) return softlimit_table[0];
    int index = get_msb_index(N) - 4;  // Offset from 2^4 = 16
    if (index < 0) return softlimit_table[0];
    if (index >= (int)(sizeof(softlimit_table))) return softlimit_table[sizeof(softlimit_table) - 1];
    return softlimit_table[index];
}



typedef struct graph_node {
    Vector *vector;
    int idegree;			// Cantidad de conexiones entrantes, evitar que quede aislado
    int odegree;  			// Cantidad de conexiones salientes actualmente
    int odegree_hardlimit;	// Limite fijo por memoria para conexiones salientes
    int odegree_softlimit;	// Limite logico para conexiones salientes

    int alive;
    struct graph_node *neighbors[];
} GraphNode;

typedef struct {
    Heap W;
    Heap C;
    Map  visited;
    int  k;
} SearchContext;

int init_search_context(SearchContext *sc, int ef, int k, int (*cmp)(float32_t, float32_t)) {
    PANIC_IF(ef < 0, "invalid parameter ef");
    PANIC_IF(k < 0, "invalid parameter k");
    if (init_heap(&sc->W, HEAP_MIN, ef, cmp) != HEAP_SUCCESS)
        return SYSTEM_ERROR;
    if (init_heap(&sc->C, HEAP_MAX, NOLIMIT_HEAP, cmp) != HEAP_SUCCESS) {
        heap_destroy(&sc->W);
        return SYSTEM_ERROR;
    }
    if (init_map(&sc->visited, 1000, 15) != MAP_SUCCESS) {
        heap_destroy(&sc->W);
        heap_destroy(&sc->C);
        return SYSTEM_ERROR;
    }
    sc->k = k;
    return SUCCESS;
}

/**
 * sc_discard_candidates - Keeps only the top-k elements in the result heap W.
 *
 * This function removes the worst-scoring elements from the max-heap W
 * until only k results remain, based on the value stored in sc->k.
 * Use this after a call to nsw_search if you want to retain exactly k results.
 */

void sc_discard_candidates(SearchContext *sc) {
    int c = heap_size(&sc->W) - sc->k;
    while ( c-- > 0 ) 
        PANIC_IF(heap_pop(&sc->W, NULL) == HEAP_ERROR_EMPTY, "lack of consistency");
}

/**
 * nsw_search - Performs a navigable small-world graph search from a given entry point.
 *
 * This function implements the core search mechanism used in NSW/HNSW-based approximate nearest neighbor graphs.
 * It traverses the graph starting from a specified entry node, using a best-first strategy guided by two heaps:
 * 
 *   - W (max-heap): stores the best `ef` candidates found so far
 *   - C (min-heap): stores candidate nodes to be explored next
 *
 * The function terminates when the best candidate in `C` cannot improve the current results in `W` (i.e., early stopping),
 * or when all possible candidates have been explored.
 * 
 * Nodes are only visited once (tracked in `sc->visited`), and neighbors marked as deleted (not alive) are skipped.
 *
 * Parameters:
 *   @entry          The starting node for the search
 *   @sc             Pointer to the SearchContext structure (holds heaps and visited map)
 *   @v              Query vector to compare against
 *   @dims_aligned   Number of dimensions of the vectors (already aligned)
 *   @cmp            Pointer to the comparison method struct, containing distance function and ranking logic
 *
 * Returns:
 *   SUCCESS on completion, or SYSTEM_ERROR if memory or insertion fails
 *
 * Pseudocode:
 *
 *   insert entry into visited
 *   compute distance(entry, query)
 *   insert entry into W and C
 *
 *   while C is not empty:
 *       current = pop C
 *       if W is full and current is worse than the worst in W:
 *           break
 *       for each neighbor of current:
 *           if neighbor is not visited and is alive:
 *               mark neighbor visited
 *               compute distance
 *               insert into C
 *               if W is full:
 *                   replace worst in W if new is better
 *               else:
 *                   insert into W
 */
int nsw_search(const GraphNode *entry, SearchContext *sc, float32_t *v, uint16_t dims_aligned, const CmpMethod *cmp) {
    GraphNode *current, *neighbor;
    float32_t distance;
    HeapNode c_node;
    HeapNode w_node;
    HeapNode n_node;
    int ret, i;
    
    ret = map_insert(&sc->visited, entry->vector->id, NULL);
    if (ret != MAP_SUCCESS) 
        return SYSTEM_ERROR;

    distance = cmp->compare_vectors(v, entry->vector->vector, dims_aligned);
    n_node.distance = distance;
    HEAP_NODE_PTR(n_node.value) = (GraphNode *)entry;

    PANIC_IF(heap_insert(&sc->W, &n_node) != HEAP_SUCCESS, "invalid heap");
    PANIC_IF(heap_insert(&sc->C, &n_node) != HEAP_SUCCESS, "invalid heap");

    while(heap_size(&sc->C) != 0) {

        PANIC_IF(heap_pop(&sc->C, &c_node)  != HEAP_SUCCESS, "lack of consistency");
        PANIC_IF(heap_peek(&sc->W, &w_node) != HEAP_SUCCESS, "lack of consistency");

        if (heap_full(&sc->W) && cmp->is_better_match(w_node.distance, c_node.distance))
            break;
        
        current = (GraphNode *) HEAP_NODE_PTR(c_node.value);
        for (i = 0; i < current->odegree; i++) {
            neighbor = current->neighbors[i];
            if (neighbor && neighbor->vector && !map_has(&sc->visited, neighbor->vector->id)) {
                ret = map_insert(&sc->visited, neighbor->vector->id, NULL);
                if (ret != MAP_SUCCESS) 
                    return SYSTEM_ERROR;

                if (!neighbor->alive)
                    continue;

                distance = cmp->compare_vectors(v, neighbor->vector->vector, dims_aligned);
                c_node.distance = distance;
                HEAP_NODE_PTR(c_node.value) = neighbor;
                PANIC_IF(heap_insert(&sc->C, &c_node) == HEAP_ERROR_FULL, "bad initialization");

                if (heap_full(&sc->W)) {
                    PANIC_IF(heap_peek(&sc->W, &w_node) == HEAP_ERROR_EMPTY, "error on peek in a full heap");
                    if (cmp->is_better_match(c_node.distance, w_node.distance))
                        PANIC_IF(heap_replace(&sc->W, &c_node) != HEAP_SUCCESS, "cannot replace worst node in W");
                } else{
                    PANIC_IF(heap_insert(&sc->W, &c_node) == HEAP_ERROR_FULL, "lack of consistency");
                }
            }
        }
    }

    sc_discard_candidates(sc);
    return SUCCESS;
}


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

    node->odegree_hardlimit = odegree_max;
    return node;
}

void lazy_delete_gnode(GraphNode *gnode) {
    PANIC_IF(gnode == NULL, "lazy delete of null gnode");
    gnode->alive = 0;
}


#endif
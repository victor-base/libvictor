#ifndef _GRAPH_H
#define _GRAPH_H 1


#include <stdint.h>
#include <math.h>
#include "vector.h"
#include "panic.h"
#include "heap.h"
#include "method.h"
#include "map.h"

#include "mem.h"

/*
* compute_odegree - Dynamic softlimit lookup for NSW/HNSW graph construction
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

static const uint8_t odegree_table[] = {
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

static inline int compute_odegree(uint64_t N) {
    if (N < 16) return odegree_table[0];
    int index = get_msb_index(N) - 4;  // Offset from 2^4 = 16
    if (index < 0) return odegree_table[0];
    if (index >= (int)(sizeof(odegree_table))) return odegree_table[sizeof(odegree_table) - 1];
    return odegree_table[index];
}

static int compute_efConstruction(uint64_t N, int M) {
    double base = pow((double)N, 0.28);
    int ef = (int)ceil(base);
    int min_ef = 3 * M;
    return ef > min_ef ? ef : min_ef;
}


static int compute_efSearch(uint64_t N, int M, int k) {
    double base = pow((double)N, 0.35);
    int ef = (int)ceil(base);
    int min_ef = 2 * M;
    int min_recall = 4 * k;

    if (ef < min_ef) ef = min_ef;
    if (ef < min_recall) ef = min_recall;
    return ef;
}



typedef struct node_nsw {
    Vector *vector;
    int idegree;			// Cantidad de conexiones entrantes, evitar que quede aislado
    int odegree;  			// Cantidad de conexiones salientes actualmente

    int alive;
    struct node_nsw *next;
    struct node_nsw *neighbors[];
} INodeNSW;

typedef struct {
    int efSearch;
    int efContruct;
    int od_hard_limit;
    int od_soft_limit;

    CmpMethod *cmp;          // Comparison method (L2 norm, cosine similarity, etc.)

    uint64_t elements;       // Number of elements stored in the index
    uint16_t dims;           // Number of dimensions for each vector
    uint16_t dims_aligned;   // Aligned dimensions for efficient memory access
    INodeNSW *gentry;
    INodeNSW *lentry;
} IndexNSW;

#define OD_PROGESIVE  0x00
#define EF_AUTOTUNED  0x00
typedef struct {
    int efSearch;
    int efContruct;
    int odegree;
} NSWContext;




typedef struct {
    Heap W;
    Heap C;
    Map  visited;
    int  k;
} SearchContext;


/**
 * init_search_context - Initializes the SearchContext used for NSW or HNSW traversal.
 *
 * This function sets up the required heaps and visited map for a vector search operation.
 * It creates:
 *   - A min-heap `W` to store the best matches found (capacity = ef)
 *   - A max-heap `C` to store candidates to be explored
 *   - A hash map `visited` to avoid revisiting nodes
 *
 * @param sc   Pointer to the SearchContext to initialize
 * @param ef   Exploration factor (ef): size of heap W and maximum candidates to track
 * @param k    Number of results to return (used to trim heap W after search)
 * @param cmp  Comparison function used to rank distances (e.g., smaller-is-better)
 *
 * @return SUCCESS on success, or SYSTEM_ERROR if memory allocation fails
 */

static int init_search_context(SearchContext *sc, int ef, int k, int (*cmp)(float32_t, float32_t)) {
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
 * destroy_search_context - Releases all resources held by a SearchContext.
 *
 * This function destroys both heaps (`W` and `C`) and the visited map,
 * cleaning up all memory used during the search process.
 *
 * @param sc Pointer to the SearchContext to destroy
 */

static void destroy_search_context(SearchContext *sc) {
    heap_destroy(&sc->W);
    heap_destroy(&sc->C);
    map_destroy(&sc->visited);
}

/**
 * sc_discard_candidates - Keeps only the top-k elements in the result heap W.
 *
 * This function removes the worst-scoring elements from the max-heap W
 * until only k results remain, based on the value stored in sc->k.
 * Use this after a call to nsw_search if you want to retain exactly k results.
 */
static void sc_discard_candidates(SearchContext *sc) {
    int c = heap_size(&sc->W) - sc->k;
    while ( c-- > 0 ) 
        PANIC_IF(heap_pop(&sc->W, NULL) == HEAP_ERROR_EMPTY, "lack of consistency");
}

/**
 * nsw_explore - Performs a navigable small-world graph search from a given entry point.
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
static int nsw_explore(const INodeNSW *entry, SearchContext *sc, float32_t *v, uint16_t dims_aligned, const CmpMethod *cmp) {
    INodeNSW *current, *neighbor;
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
    HEAP_NODE_PTR(n_node.value) = (INodeNSW *)entry;

    PANIC_IF(
        heap_insert(&sc->W, &n_node) != HEAP_SUCCESS,
        "invalid heap"
    );
    PANIC_IF(
        heap_insert(&sc->C, &n_node) != HEAP_SUCCESS, 
        "invalid heap"
    );

    while(heap_size(&sc->C) != 0) {

        PANIC_IF(
            heap_pop(&sc->C, &c_node)  != HEAP_SUCCESS, 
            "lack of consistency"
        );
        PANIC_IF(
            heap_peek(&sc->W, &w_node) != HEAP_SUCCESS, 
            "lack of consistency"
        );

        if (heap_full(&sc->W) && cmp->is_better_match(w_node.distance, c_node.distance))
            break;
        
        current = (INodeNSW *) HEAP_NODE_PTR(c_node.value);
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
                
                PANIC_IF(
                    heap_insert(&sc->C, &c_node) == HEAP_ERROR_FULL, 
                    "bad initialization"
                );

                if (heap_full(&sc->W)) {
                    PANIC_IF(
                        heap_peek(&sc->W, &w_node) == HEAP_ERROR_EMPTY,
                        "error on peek in a full heap"
                    );
                    if (cmp->is_better_match(c_node.distance, w_node.distance)) {
                        PANIC_IF(
                            heap_replace(&sc->W, &c_node) != HEAP_SUCCESS, 
                            "cannot replace worst node in W"
                        );
                    }
                } else{
                    PANIC_IF(
                        heap_insert(&sc->W, &c_node) == HEAP_ERROR_FULL, 
                        "lack of consistency"
                    );
                }
            }
        } /* for */
    } /* while */

    sc_discard_candidates(sc);
    return SUCCESS;
}

/**
 * nsw_search - Search for the top closest neighbors in a NSW index.
 *
 * @param index   Pointer to the IndexNSW structure
 * @param vector  Query vector
 * @param dims    Number of dimensions (must match index)
 * @param result  Output array of MatchResult[n], sorted by ascending distance
 *
 * @returns ErrorCode (SUCCESS or failure reason)
 */
static int nsw_search(void *index, float32_t *vector, uint16_t dims, MatchResult *result) {
    return nsw_search_n(index, vector, dims, result, 1);
}

/**
 * nsw_search_n - Search for the top-N closest neighbors in a NSW index.
 *
 * @param index   Pointer to the IndexNSW structure
 * @param vector  Query vector
 * @param dims    Number of dimensions (must match index)
 * @param result  Output array of MatchResult[n], sorted by ascending distance
 * @param n       Number of results to return
 *
 * @returns ErrorCode (SUCCESS or failure reason)
 */
static int nsw_search_n(void *index, float32_t *vector, uint16_t dims, MatchResult *result, int n) {
    IndexNSW *idx = (IndexNSW *) index;
    INodeNSW *entry;
    SearchContext sc;
    float32_t *v;
    int ef, ret;

    if (index == NULL) 
        return INVALID_INDEX;
    if (vector == NULL)
        return INVALID_VECTOR;
    if (dims != idx->dims) 
        return INVALID_DIMENSIONS;
    if (result == NULL)
        return INVALID_RESULT;

    entry = idx->gentry;
    if (entry == NULL)
        return INDEX_EMPTY;

    v = (float32_t *) aligned_calloc_mem(16, idx->dims_aligned * sizeof(float32_t));
	if (v == NULL)
        return SYSTEM_ERROR;

    memcpy(v, vector, dims * sizeof(float32_t));

    if (idx->efSearch == EF_AUTOTUNED)
        ef = compute_efSearch(idx->elements, idx->od_soft_limit, n);
    else
        ef = idx->efSearch;
    if (init_search_context(&sc, ef, n, idx->cmp->is_better_match) != SUCCESS) {
        free_aligned_mem(v);
        return SYSTEM_ERROR;
    }
		

    if ((ret = nsw_explore(entry,&sc,v,idx->dims_aligned, idx->cmp)) == SUCCESS)
        for (int k = heap_size(&sc.W); k > 0; k = heap_size(&sc.W)) {
            HeapNode node;
            heap_pop(&sc.W, &node);
            result[k-1].distance = node.distance;
            result[k-1].id = ((INodeNSW *)HEAP_NODE_PTR(node.value))->vector->id;
        }

    destroy_search_context(&sc);
    free_aligned_mem(v);
    return ret;
}

/*
 * nsw_release - Releases all resources associated with a flat index.
 *
 * @param index - Pointer to the index to be released.
 *
 * @return SUCCESS on success, INVALID_INDEX if index is NULL.
 */
static int nsw_release(void **index) {
    if (!index || !*index)
        return INVALID_INDEX;

    IndexNSW *idx = (IndexNSW *)*index;
    INodeNSW *ptr;

    ptr = idx->lentry;
    while (ptr) {
        idx->lentry = ptr->next;
        idx->elements--;
        free_vector(&ptr->vector);
        free_mem(ptr);
        ptr = idx->lentry;
    }

    free_mem(idx);  
    *index = NULL;
    return SUCCESS;
}



#define GRAPH_NODESZ(M) sizeof(INodeNSW) + (sizeof(INodeNSW *) * (M))

INodeNSW *new_gnode(uint64_t id, float32_t *vector, uint16_t dims, int odegree_max) {
    INodeNSW *node = (INodeNSW *) calloc_mem(1, GRAPH_NODESZ(odegree_max));

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

void lazy_delete_gnode(NSWNode *gnode) {
    PANIC_IF(gnode == NULL, "lazy delete of null gnode");
    gnode->alive = 0;
}


#endif
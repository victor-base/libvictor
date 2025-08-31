#include <math.h>
#include "graph.h"
#include "vector.h"
#include "victor.h"
#include "mem.h"
#include "map.h"



/**
 * @brief Generates a random float uniformly distributed in the interval (0, 1).
 *
 * Avoids exact 0 or 1 by adding 1 and dividing by (RAND_MAX + 2).
 *
 * @return A float in the range (0, 1).
 */
static inline float random_uniform_0_1() {
    return ((float) rand() + 1.0f) / ((float) RAND_MAX + 2.0f);
}

/**
 * @brief Computes the inverse of the natural logarithm of M.
 *
 * Used in the level assignment function for HNSW graphs.
 *
 * @param M The connectivity parameter of the HNSW graph.
 * @return The value of 1 / logf(M).
 */
static inline float lm(int M) {
    return 1.0f / logf((float) M);
}

/**
 * @brief Assigns a level to a new node in the HNSW graph.
 *
 * The level is sampled from an exponential distribution based on M0.
 * This determines how many layers the new node will participate in.
 *
 * @param M0 Maximum number of connections at level 0.
 * @return An integer representing the level of the node.
 */
static inline int assign_level(int M0) {
    float r = -logf(random_uniform_0_1()) * lm(M0 / 2);
    return (int) r;
}





/**
 * alloc_gnode - Allocates a GraphNode structure with all internal arrays in a single memory block.
 *
 * This function allocates a GraphNode and its associated internal layout:
 *   - The `GraphNode` struct itself
 *   - A `Degrees[]` array of size (level + 1)
 *   - A `neighbors[]` array of GraphNode** pointers (per level)
 *   - Contiguous neighbor arrays: one for each level
 *       - Level 0 stores up to `M0` neighbors
 *       - Levels > 0 store up to `M0 / 2` neighbors each
 *
 * Memory layout (single `calloc`):
 *   | GraphNode | Degrees[L+1] | neighbors[L+1] | neighbor arrays |
 *
 * The vector is allocated via `make_vector()` and linked to the node.
 *
 * @param id             Unique vector identifier
 * @param vector         Pointer to the raw vector values
 * @param dims_aligned   Number of dimensions (padded for SIMD alignment)
 * @param M0             Max number of neighbors for level 0
 * @param level          Max level for this node (inclusive)
 *
 * @return Pointer to the allocated GraphNode, or NULL on failure
 */

GraphNode *alloc_graph_node(uint64_t id, uint64_t tag, float32_t *vector, uint16_t dims_aligned, int M0) {
    GraphNode *node = NULL;
    int M = M0 / 2;
    int level = assign_level(M0);
    size_t sz = 0;

    sz += sizeof(GraphNode);
    sz += (level + 1) * sizeof(Degrees);
    sz += (level + 1) * sizeof(GraphNode **);
    sz += (M0 + ((level) * M)) * sizeof(GraphNode *);

    node = (GraphNode *)calloc_mem(1, sz);
    if (!node) 
        return NULL;

	if (vector && id != NULL_ID) {
		node->vector = make_vector(id, tag, vector, dims_aligned);
		if (!node->vector) {
			free_mem(node);
			return NULL;
		}
	} else {
		node->vector = NULL;
	}
    uint8_t *ptr = (uint8_t *)(node + 1);

    node->level = level;
    node->alive = 1;

    node->degrees = (Degrees *) ptr;
    ptr += (level + 1) * sizeof(Degrees);

    node->neighbors = (GraphNode ***) ptr;
    ptr += (level + 1) * sizeof(GraphNode **);

    for (int l = 0; l <= level; l++) {
        int n = (l == 0) ? M0 : M;
        node->neighbors[l] = (GraphNode **) ptr;
        ptr += n * sizeof(GraphNode *);
        node->degrees[l].idegree = 0;
        node->degrees[l].odegree = 0;
    }

    return node;
}

/**
 * free_gnode - Frees a GraphNode and its associated vector.
 *
 * This function releases both the vector and the node memory.
 * Safe to call with NULL or already-freed nodes.
 *
 * @param g Pointer to a GraphNode pointer. Will be NULL'd after free.
 */
void free_graph_node(GraphNode **g) {
    if (!g || !*g)
        return;
    if ((*g)->vector) {
        free_vector(&(*g)->vector);
        (*g)->vector = NULL;
    }
    free_mem(*g);
    *g = NULL;
}


/**
 * BFSContext - Internal state used during vector search traversal.
 *
 * This structure encapsulates the context required for a single search operation,
 * including the candidate heaps, visited set, query vector, and control parameters.
 * It is initialized per query and can be reused between different searches if cleared.
 */
typedef struct {
    float32_t *query;       /* Aligned pointer to the query vector. Used for distance comparisons. */
    uint16_t  dims_aligned; /* Padded/aligned number of dimensions (mod 4 or 16 for SIMD use). */
    CmpMethod *cmp;         /* Pointer to distance comparison function (e.g., L2, cosine, dot). */

	// flags
	int filter_alive;
} SearchContext;

#define SELECT_NEIGHBORS_SIMPLE     0x00
#define SELECT_NEIGHBORS_HEURISTIC  0x01
#define HEURISTIC_EXTEND_CANDIDATES 1 << 2
#define HEURISTIC_KEEP_PRUNED       1 << 3


/**
 * set_candidate_queue - Copies candidates from C to W and optionally extends them.
 *
 * This function consumes all elements from heap C, moving them into W.
 * If HEURISTIC_EXTEND_CANDIDATES is set, it also adds all neighbors of each
 * candidate into W, avoiding duplicates using a temporary map.
 *
 * Note:
 *   - This function **empties** the input heap C.
 *   - C is assumed to be temporary and will be repopulated later by caller.
 *   - Elements are inserted into W in priority order (heap insert).
 *
 * Parameters:
 *   @C         Heap of initial candidates (will be emptied)
 *   @W         Output heap with extended working candidates
 *   @M         Maximum number of final neighbors (not enforced here)
 *   @heuristic Bitmask with heuristics
 *   @level     Current graph level
 *   @sc        Pointer to search context (distance + query)
 *
 * Returns:
 *   SUCCESS or SYSTEM_ERROR
 */
static int set_candidate_queue(Heap *C, Heap *W, int heuristic, int level, SearchContext *sc) {
    GraphNode *c = NULL;
    GraphNode *n = NULL;
    HeapNode e;
    HeapNode eAdj;
    float32_t dist;
    Map inW = MAP_INIT();
    int ret = SYSTEM_ERROR;

    if (heuristic & HEURISTIC_EXTEND_CANDIDATES)
        if (init_map(&inW, 500, 20) != MAP_SUCCESS)
            return SYSTEM_ERROR;

    /*
     *   W ← C // working queue for the candidates
     *   if extendCandidates // extend candidates by their neighbors
     *       for each e ∈ C
     *           for each eadj ∈ neighbourhood(e) at layer lc
     *               if eadj ∉ W
     *                   W ← W ⋃ eadj
     */

    while (heap_size(C) > 0) {
        PANIC_IF(heap_pop(C, &e) != HEAP_SUCCESS, "lack of consistency");

        /* W <- e */
        if (heap_insert(W, &e) != HEAP_SUCCESS)
            goto cleanup;
        
        /*
         *   The heuristic has two additional parameters:
         *   extendCandidates (set to false by default) which extends
         *   the candidate set and useful only for extremely clustered
         *   data. 
         */
        if (heuristic & HEURISTIC_EXTEND_CANDIDATES) {
            c = (GraphNode *)HEAP_NODE_PTR(e);
            
            /* Set c as element of W */
            if (map_insert_p(&inW, c->vector->id, NULL) != MAP_SUCCESS)
                goto cleanup;

            /* For each neighbor of c */
            for (int i = 0; i < (int)ODEGREE(c, level); i++) {

                n = NEIGHBOR_AT(c, level, i);
                if (n && n->vector && !map_has(&inW, n->vector->id)) {
                    /* Set n as element of W */
                    if (map_insert_p(&inW, n->vector->id, NULL) != MAP_SUCCESS)
                        goto cleanup;

                    /* Insert n into W*/
                    dist = sc->cmp->compare_vectors(sc->query, n->vector->vector, sc->dims_aligned);
                    eAdj = HEAP_NODE_SET_PTR(n, dist);
                    if (heap_insert(W, &eAdj) != HEAP_SUCCESS)
                        goto cleanup;
                }
            }
        }
    }
    ret = SUCCESS;
cleanup:
    map_destroy(&inW);
    return ret;
}

/**
 * select_neighbors_heuristic - Applies the angular diversity heuristic 
 * to select up to `k` neighbors from a working candidate set.
 *
 * This function implements the HNSW neighbor selection heuristic described in the paper,
 * with a corrected interpretation of the pruning rule:
 * 
 * For each candidate `e` extracted from the working set `W` (ordered by distance to `q`),
 * it is accepted into the result set `R` only if its distance to every already accepted node
 * is greater than its distance to the query. This prevents redundancy and promotes spatial diversity.
 *
 * Additionally, it supports two optional behaviors:
 *
 * - HEURISTIC_EXTEND_CANDIDATES: expands the candidate set by visiting each candidate's neighbors.
 * - HEURISTIC_KEEP_PRUNED: stores pruned candidates in a secondary heap `Wd` and optionally fills
 *   `R` with those if `R` has less than `k` elements after pruning.
 *
 * The final result set is written to `sc->W`, replacing any prior contents.
 *
 * @param sc      Pointer to the search context, containing query, k, heuristics, comparator, etc.
 * @param level   Graph level in which the candidate neighbors are being selected.
 * 
 * @return SUCCESS on success, or SYSTEM_ERROR on allocation failure or internal errors.
 */
static int select_neighbors_heuristic(SearchContext *sc, Heap *C, int M, int heuristic, int level) {
    GraphNode *element;
    GraphNode *choosen;
    float32_t er_dist;
    HeapNode *R = NULL;
    HeapNode e;
    Heap W  = HEAP_INIT(); // Working Queue
    Heap Wd = HEAP_INIT(); // Discarted
    int r = 0, ret = SYSTEM_ERROR;


    /*
     * R <- 0
     * W <- C  // working queue for the candidates
     * Wd <- 0 // queue for the discarded candidates 
     */
    if ((R = (HeapNode *) calloc_mem(M, sizeof(HeapNode))) == NULL)
        return SYSTEM_ERROR;

    if (init_heap(&W, HEAP_BETTER_TOP, NOLIMIT_HEAP, sc->cmp->is_better_match) != HEAP_SUCCESS)
        goto free_resources;
    
    if (set_candidate_queue(C, &W, heuristic, level, sc) != SUCCESS)
        goto free_resources;

    if (heuristic & HEURISTIC_KEEP_PRUNED &&
        init_heap(&Wd, HEAP_BETTER_TOP, NOLIMIT_HEAP, sc->cmp->is_better_match) != HEAP_SUCCESS)
        goto free_resources;
    
    
    while (heap_size(&W) > 0 && r < M) {
        PANIC_IF(heap_pop(&W, &e) != HEAP_SUCCESS, "W inconsistency");

        element = (GraphNode *) HEAP_NODE_PTR(e);

        // Compare with all r <- R
        int accept = 1;
        for (int i = 0; i < r; i++) {
            choosen = (GraphNode *) HEAP_NODE_PTR(R[i]);
            er_dist = sc->cmp->compare_vectors(element->vector->vector, choosen->vector->vector, sc->dims_aligned);
            if (sc->cmp->is_better_match(er_dist, e.distance)) {
                accept = 0;
                break;
            }
        }

        if (accept) {
            R[r++] = e;
        } else if (heuristic & HEURISTIC_KEEP_PRUNED) {
            heap_insert(&Wd, &e);
        }
    }

    /*
     *  while │Wd│> 0 and │R│< M
     *      R <- R u extract nearest element from Wd to q
     */
    if (heuristic & HEURISTIC_KEEP_PRUNED)
        for (; r < M && heap_size(&Wd) > 0; r++) 
            PANIC_IF(heap_pop(&Wd, &R[r]) != HEAP_SUCCESS, "lack of consistency");

    for (int i = 0; i < r; i++) 
        PANIC_IF(heap_insert(C, &R[i])!= HEAP_SUCCESS, "C inconsistency");

    ret = SUCCESS;

free_resources:
    if (R) free_mem(R);
    heap_destroy(&W);
    heap_destroy(&Wd);
    return ret;
}


/**
 * backlink_connect_with_shrink - Adds a reverse connection using neighbor pruning.
 *
 * This function ensures that node `n` links back to node `e`, while enforcing
 * the limit `M` on outgoing neighbors of `n`. If `n` has space, the link is added directly.
 * Otherwise:
 *   - All current neighbors of `n` are evaluated and stored in a temporary heap.
 *   - The new candidate `e` is also added to the heap.
 *   - `select_neighbors_heuristic()` is invoked to shrink the heap to the top `M` candidates
 *     while maintaining diversity (angular heuristic).
 *   - The outgoing list of `n` is replaced with the new pruned list, and corresponding
 *     in-degree counters updated.
 *
 * @param n             Node to which the backlink should be added.
 * @param e             Candidate node to be linked.
 * @param level         Level of the graph where the connection applies.
 * @param M             Maximum number of outgoing neighbors allowed.
 * @param sc            Search context (contains comparator and parameters).
 *
 * @return SUCCESS if the operation completes successfully, otherwise SYSTEM_ERROR.
 */
static int backlink_connect_with_shrink(SearchContext *sc, GraphNode *n, GraphNode *e, int level, int M) {
    GraphNode *c = NULL;
    HeapNode  node;
    float32_t d;
    Heap W = HEAP_INIT();
    int o, i;
    int ret = SYSTEM_ERROR;
    if ((o = ODEGREE(n, level)) < M) {
        NEIGHBOR_AT(n, level, o) = e;
        ODEGREE(n,level)++;
        IDEGREE(e,level)++;
        return SUCCESS;
    }

    if (init_heap(&W, HEAP_WORST_TOP, M+1, sc->cmp->is_better_match) != HEAP_SUCCESS)
        return SYSTEM_ERROR;

    // Firt loop, clean all the output connections measuring the distance and push to queue
    for (i = 0; i < o; i++) {
        c = NEIGHBOR_AT(n,level,i);
        if (c && c->vector) {
            d = sc->cmp->compare_vectors(c->vector->vector, n->vector->vector, sc->dims_aligned);
            node = HEAP_NODE_SET_PTR(c, d);
            if (heap_insert(&W, &node)!= HEAP_SUCCESS)
                goto clean_return;
            IDEGREE(c, level)--;
            ODEGREE(n, level)--;
            NEIGHBOR_AT(n, level, i) = NULL;
        } else {
            PANIC_IF(1==1, "c && c->vector");
        }
    }
    // Now add e to the queue
    d = sc->cmp->compare_vectors(e->vector->vector, n->vector->vector, sc->dims_aligned);
    node = HEAP_NODE_SET_PTR(e, d);
    if (heap_insert(&W, &node)!= HEAP_SUCCESS)
        goto clean_return;

    if (select_neighbors_heuristic(sc, &W, M, HEURISTIC_KEEP_PRUNED, level)!= SUCCESS)
        goto clean_return;

    i = 0;
    while (heap_size(&W) > 0) {
        PANIC_IF(i == M, "wrong number of neighbors");
        PANIC_IF(heap_pop(&W, &node) != HEAP_SUCCESS, "lack of consistency");
        c = (GraphNode *) HEAP_NODE_PTR(node);
        NEIGHBOR_AT(n, level, i) = c;
        ODEGREE(n,level)++;
        IDEGREE(c,level)++;
        i++;
    }
    ret = SUCCESS;
clean_return:
    heap_destroy(&W);
    return ret;
}


/**
 * connect_to - Creates a directed edge from node `e` to node `n`.
 *
 * If the number of outgoing neighbors of `e` at the given level is below the limit `M`,
 * the function adds `n` as a neighbor. It then invokes the backlink procedure to 
 * potentially connect `n` to `e` with pruning if needed.
 *
 * @param e      Source node initiating the connection.
 * @param n      Target node to be added as neighbor.
 * @param level  Graph level in which the connection takes place.
 * @param M      Maximum number of neighbors allowed.
 * @param sc     Pointer to search context (contains comparator and parameters).
 *
 * @return SUCCESS if the connection was successful, or an error code on failure.
 */
static int connect_to(SearchContext *sc, GraphNode *e, GraphNode *n, int level, int M) {
    int i = ODEGREE(e, level);
    PANIC_IF(i == M, "invalid odegree");
    NEIGHBOR_AT(e,level,i) = n;
    ODEGREE(e, level)++;
    IDEGREE(n, level)++;
    return backlink_connect_with_shrink(sc, n, e, level, M);
}



/**
 * select_neighbors - Finalizes the neighbor selection process from a candidate heap.
 *
 * This function selects up to `M` neighbors from a heap `W` using either a naive cutoff strategy
 * or a heuristic-based approach for angular diversity.
 *
 * If `SELECT_NEIGHBORS_HEURISTIC` is enabled, the function delegates to 
 * `select_neighbors_heuristic()` to apply angular diversity pruning as described in the HNSW paper.
 * Otherwise, it simply retains the top `M` elements from the heap and discards the rest.
 *
 * This method is typically used during index construction (e.g., during neighbor connection or
 * graph refinement) to enforce maximum degree constraints.
 *
 * @param W              Pointer to the candidate heap.
 * @param M              Maximum number of neighbors to retain.
 * @param heuristic      Selection strategy (e.g., SELECT_NEIGHBORS_HEURISTIC or default).
 * @param level          Current graph level (used only in heuristic mode).
 * @param cmp            Pointer to the comparison method (for distance and match rules).
 * @param query          Pointer to the aligned query vector.
 * @param dims_aligned   Number of aligned dimensions in the query vector.
 * 
 * @return SUCCESS if the heap was reduced successfully, or SYSTEM_ERROR if an inconsistency occurs.
 */
static int select_neighbors(SearchContext *sc, Heap *W, int M, int heuristic, int level) {
    int c;
    
    if (heuristic == SELECT_NEIGHBORS_HEURISTIC)
        return select_neighbors_heuristic(sc, W, M, heuristic, level);

    c = heap_size(W) - M;
    while ( c-- > 0 ) 
        PANIC_IF(heap_pop(W, NULL) == HEAP_ERROR_EMPTY, "lack of consistency");

    return SUCCESS;
}


/**
 * search_layer - Performs best-first traversal on the graph at a given level.
 *
 * This function explores the graph from a set of entry points (`ep`) using a
 * candidate queue (`C`, max-heap) and a result set (`W`, min-heap). It performs
 * up to `ef` evaluations and keeps track of visited nodes to avoid revisits.
 * Only alive and unvisited neighbors are considered for expansion.
 *
 * Input:
 *   - `sc`:      Search context (query vector, comparator, dimensions, etc.)
 *   - `ep`:      Array of entry nodes to start the traversal from.
 *   - `len`:     Number of entry nodes in `ep`.
 *   - `ef`:      Search breadth (maximum number of node evaluations).
 *   - `level`:   Graph level used for neighbor access.
 *   - `W`:       Output heap (min-heap of best `k` candidates found so far).
 *
 * Output:
 *   - The heap `W` will contain up to `k` best candidate nodes (based on the
 *     comparator function). This heap is filled during traversal and is the
 *     final output of the search layer.
 *
 * Return:
 *   - SUCCESS (0) if the traversal completes without memory errors.
 *   - SYSTEM_ERROR  if a memory allocation or map operation fails.
 *
 * Algorithm:
 *   1. For each entry node:
 *      - Compute distance to query.
 *      - Insert into `visited`, `C`, and `W`.
 *   2. While `C` (candidate heap) is not empty:
 *      - Pop top candidate `current`.
 *      - If `W` is full and `current` is worse than worst in `W`, exit early.
 *      - For each neighbor of `current`:
 *          - If neighbor is unvisited and alive:
 *              - Mark as visited.
 *              - Compute distance to query.
 *              - Insert into `C`.
 *              - If `W` is full:
 *                  - Replace worst if new is better.
 *                Else:
 *                  - Insert into `W`.
 */
static int search_layer(SearchContext *sc, GraphNode **ep, int len, int ef, int level, Heap *W) {
    Map  visited = MAP_INIT();
    Heap C = HEAP_INIT();
    GraphNode *current = NULL, *neighbor = NULL;
    HeapNode c = HEAP_NODE_NULL();
    HeapNode w = HEAP_NODE_NULL(); 
    HeapNode n = HEAP_NODE_NULL();
    float32_t d;
    int ret = SYSTEM_ERROR, i;

    if (init_map(&visited, 1000, 15) != MAP_SUCCESS)
        goto cleanup_return;

    if (init_heap(&C, HEAP_BETTER_TOP, NOLIMIT_HEAP, sc->cmp->is_better_match)!= HEAP_SUCCESS) 
        goto cleanup_return;

    if (init_heap(W, HEAP_WORST_TOP, ef, sc->cmp->is_better_match)!= HEAP_SUCCESS)
        goto cleanup_return;

    for (i = 0; i < len; i++) {
        current = ep[i];
        if (current && current->vector) {
            d = sc->cmp->compare_vectors(current->vector->vector, sc->query, sc->dims_aligned);
            n = HEAP_NODE_SET_PTR(current, d);
            ret = map_insert_p(&visited, current->vector->id, NULL);
            if (ret != MAP_SUCCESS) {
                heap_destroy(W);
                goto cleanup_return;
            }
            PANIC_IF(heap_insert(&C, &n) != HEAP_SUCCESS, "invalid heap");
            if (!sc->filter_alive || current->alive)
            	PANIC_IF(heap_insert(W, &n)  != HEAP_SUCCESS, "invalid heap");
        }
    }

    while(heap_size(&C) > 0) {

        PANIC_IF(heap_pop(&C, &c)  != HEAP_SUCCESS, "lack of consistency");

		if (heap_size(W) > 0) {
			PANIC_IF(heap_peek(W, &w) != HEAP_SUCCESS, "lack of consistency");
			if (heap_full(W) && sc->cmp->is_better_match(w.distance, c.distance)) 
				break;
		}
        
        current = (GraphNode *) HEAP_NODE_PTR(c);
        for (i = 0; i < (int) ODEGREE(current, level); i++) {
            neighbor = NEIGHBOR_AT(current, level, i);
            if (neighbor != NULL && neighbor->vector && !map_has(&visited, neighbor->vector->id)) {
                
                ret = map_insert_p(&visited, neighbor->vector->id, NULL);
                if (ret != MAP_SUCCESS) {
                    heap_destroy(W);
                    goto cleanup_return;
                }
                d = sc->cmp->compare_vectors(sc->query, neighbor->vector->vector, sc->dims_aligned);
                n = HEAP_NODE_SET_PTR(neighbor, d);
				
				if (heap_size(W) > 0) {
					PANIC_IF(heap_peek(W, &w) != HEAP_SUCCESS, "lack of consistency");
				}
				if (!heap_full(W) || sc->cmp->is_better_match(d, w.distance)) {	
                    PANIC_IF(heap_insert(&C, &n) == HEAP_ERROR_FULL, "bad initialization");
                }
                
                if (!sc->filter_alive || neighbor->alive) {
                    if (heap_full(W)) {
						PANIC_IF(heap_peek(W, &w) != HEAP_SUCCESS, "lack of consistency");
                        if (sc->cmp->is_better_match(n.distance, w.distance)) {
                            PANIC_IF(heap_replace(W, &n) != HEAP_SUCCESS, "cannot replace worst node in W");
                        }
                    } else{
                        PANIC_IF(heap_insert(W, &n) == HEAP_ERROR_FULL, "lack of consistency");
                    }
                }
            }
        } /* for */
    } /* while */
    ret = SUCCESS;
cleanup_return:
    map_destroy(&visited);
    heap_destroy(&C);
    return ret;
}


/**
 * @brief Inserts a new node into the HNSW graph index.
 *
 * This function adds a new vector and its associated ID into the Hierarchical
 * Navigable Small World (HNSW) structure. The insertion procedure includes
 * hierarchical navigation from the top level to the bottom, followed by local
 * connection and neighborhood updates based on distance heuristics.
 *
 * The new node is assigned a level according to an exponential distribution.
 * It is then inserted into the graph by:
 *   - Searching at each level from the top layer down to the node’s level + 1,
 *     using a greedy search (ef = 1) to track the best entry point.
 *   - Performing a wider search (ef_construction) at levels ≤ node->level to
 *     retrieve candidate neighbors.
 *   - Selecting the best neighbors using diversity heuristics.
 *   - Creating bidirectional connections between the new node and the selected
 *     neighbors at each level.
 *
 * The function also maintains a flat linked list of all inserted nodes to enable
 * full traversal (e.g., for brute-force recall evaluation or serialization).
 *
 * This implementation assumes the graph is initialized and that all
 * memory allocation routines are handled externally (e.g., calloc_mem,
 * alloc_gnode, free_mem).
 *
 * Parameters:
 *   @index   Pointer to the IndexHNSW structure (cast to void* for genericity).
 *   @id      Unique identifier for the new vector.
 *   @vector  Aligned pointer to the float32_t embedding to insert.
 *   @dims    Number of dimensions in the input vector (must match index config).
 *   @ref     Optional output pointer to receive the inserted GraphNode.
 *
 * Returns:
 *   SUCCESS (0) on successful insertion.
 *   INVALID_DIMENSIONS if vector dimension mismatches index.
 *   SYSTEM_ERROR if memory allocation or graph operations fail.
 *
 * Preconditions:
 *   - Index must be initialized.
 *   - Vector must be aligned and of matching dimensionality.
 *
 * Postconditions:
 *   - Node is added to all relevant levels and connected appropriately.
 *   - Index state (entry point, top level, size) is updated.
 *   - `ref` will point to the inserted node if non-null.
 *
 * Complexity:
 *   - O(log N) for hierarchical descent.
 *   - O(ef_construction × log ef_construction) for neighbor selection per level.
 */
int graph_insert(IndexHNSW *idx, GraphNode *node) {
    SearchContext sc;
    GraphNode **entry;
    HeapNode w;
    Heap W = HEAP_INIT();
    int ret, i, e, m;

    if (idx->elements == 0) {
        idx->elements = idx->elements + 1;
        idx->gentry = node;
        idx->head = node;
        idx->top_level = node->level;
        return SUCCESS;
    }
    
    // Insert in the flat list
    node->next = idx->head;
    idx->head = node;

    // Fill Search Context
    sc.cmp   = idx->cmp;
    sc.query = node->vector->vector;
    sc.dims_aligned   = idx->dims_aligned;
	sc.filter_alive = 0;
	entry = calloc_mem(idx->M0, sizeof(GraphNode *));
    if (!entry)
        goto return_with_error;
   /*
    *  for lc <- L ... l+1
    *      W  <- SEARCH-LAYER(q, ep, ef=1, lc)
    *      ep <- get the nearest element from W to q
    */
    entry[0] = idx->gentry;
    for (i = idx->top_level; i > node->level; i--) {
        if (search_layer(&sc, entry, 1, 1, i, &W) != SUCCESS)
            goto return_with_error;
    
        PANIC_IF(heap_size(&W) != 1, "assertion in search layer");
        PANIC_IF(heap_pop(&W, &w) != HEAP_SUCCESS, "invalid pop");
        entry[0] = (GraphNode *) HEAP_NODE_PTR(w);
        heap_destroy(&W);
    }


    /*
     * for lc <- min(L, l) … 0
     *   W <- SEARCH-LAYER(q, ep, efConstruction, lc)
     *   neighbors <- SELECT-NEIGHBORS(q, W, M, lc) // alg. 3 or alg. 4
     *   add bidirectionall connectionts from neighbors to q at layer lc
     *   for each e E neighbors // shrink connections if needed
     *       eConn <- neighbourhood(e) at layer lc
     *       if │eConn│ > Mmax // shrink connections of e
     *           // if lc = 0 then Mmax = Mmax0
     *           eNewConn <- SELECT-NEIGHBORS(e, eConn, Mmax, lc)
     *       // alg. 3 or alg. 4
     *       set neighbourhood(e) at layer lc to eNewConn
     *  ep ← W
     */

    e = 1;
    for ( ; i >= 0; i--) {
        if (search_layer(&sc, entry, e, idx->ef_construct, i, &W) != SUCCESS)
            goto return_with_error;
    
        m = i==0 ? idx->M0: idx->M0/2;
        ret = select_neighbors(&sc, &W, m, 
                              SELECT_NEIGHBORS_HEURISTIC | HEURISTIC_KEEP_PRUNED | HEURISTIC_EXTEND_CANDIDATES, i);

        if ( ret != SUCCESS )
            goto return_with_error;

        PANIC_IF(heap_size(&W) > m, "invalid neighbors selection");

        e = 0;
        while (heap_size(&W) > 0) {
            PANIC_IF(heap_pop(&W, &w) != HEAP_SUCCESS, "invalid pop");

            PANIC_IF((entry[e] = HEAP_NODE_PTR(w)) == NULL, "invalid neighbor");
            if (connect_to(&sc, node, entry[e], i, m) != SUCCESS)
                goto return_with_error;
            e = e + 1;
        }
        heap_destroy(&W);
    }
    idx->elements = idx->elements + 1;
    if (node->level > idx->top_level) {
        idx->gentry = node;
        idx->top_level = node->level;
    }
    free_mem(entry);
    return SUCCESS;
return_with_error:
    heap_destroy(&W);
    free_mem(entry);
    return SYSTEM_ERROR;
}

/*
 * flat_linear_search - Finds the top-N closest matches in a flat index with optional tag filtering.
 *
 * Performs a linear search over a linked list of indexed vectors, identifying the top-N closest
 * matches to a given query vector. Uses a heap to efficiently keep the best results and supports
 * filtering by tag (bitmask).
 *
 * @param current      Pointer to the head of the linked list of INodeFlat.
 * @param tag          Bitmask filter: only vectors whose tag shares at least one bit will be considered.
 *                     If tag == 0, no tag filtering is applied.
 * @param v            Pointer to the query vector.
 * @param dims_aligned Number of aligned dimensions in the vector.
 * @param result       Output array of MatchResult to store the best matches.
 * @param n            Number of top matches to return.
 * @param cmp          Pointer to the CmpMethod structure for distance comparison.
 * @return SUCCESS if the search was successful, SYSTEM_ERROR on memory error.
 */
int graph_linear_search(IndexHNSW *idx, uint64_t tag, float32_t *restrict v, MatchResult *result, int n) {
    Heap heap = HEAP_INIT();
    HeapNode node;
	GraphNode *current = idx->head;

	if (!current) 
		return SUCCESS;

    if (init_heap(&heap, HEAP_WORST_TOP, n, idx->cmp->is_better_match) == HEAP_ERROR_ALLOC)
        return SYSTEM_ERROR;
    
    int i, k;
    for (i = 0; i < n; i++) {
        result[i].distance = idx->cmp->worst_match_value;
        result[i].id = NULL_ID;
    }
    while (current) {
		if (!tag || (tag & current->vector->tag )) {
			node.distance = idx->cmp->compare_vectors(current->vector->vector, v, idx->dims_aligned);
			HEAP_NODE_PTR(node) = current;
			PANIC_IF(heap_insert_or_replace_if_better(&heap, &node) != HEAP_SUCCESS, "error in heap");
		}
		current = current->next;
    }

    k = heap_size(&heap);
	while (k > 0) {
		heap_pop(&heap, &node);
		result[--k].distance = node.distance;
		result[k].id = ((GraphNode *)HEAP_NODE_PTR(node))->vector->id;
	}
    heap_destroy(&heap);
    return SUCCESS;
}


/**
 * @brief Performs approximate nearest neighbor search in HNSW index.
 *
 * This function traverses the HNSW graph hierarchy from the top layer down
 * to level 0, using greedy best-first search at each layer to locate the
 * region where the query vector is likely to reside. Once at level 0, it
 * performs a broader search using `ef_search` candidates to retrieve the
 * top `n` closest nodes to the query vector.
 *
 * Results are returned in `result[]`, sorted from best match (lowest distance)
 * at index 0 to worst match at index n-1.
 *
 * Parameters:
 *   @index   Pointer to a valid IndexHNSW structure.
 *   @vector  Aligned pointer to input vector of dimension `dims`.
 *   @dims    Number of dimensions in the input vector.
 *   @R       Top Best Heap size `n` to store the closest matches.
 *   @n       Maximum number of matches to return (top-k).
 *
 * Returns:
 *   SUCCESS (0) on success.
 *   INVALID_DIMENSIONS if `dims` does not match index configuration.
 *   SYSTEM_ERROR if memory allocation or internal search fails.
 *
 * Preconditions:
 *   - `vector` must be aligned and contain `dims` float32_t values.
 *   - `result[]` must be allocated by the caller with at least `n` entries.
 *
 * Postconditions:
 *   - `result[0..n-1]` contains the nearest nodes with distances to `vector`.
 *   - The output is sorted by ascending distance.
 *
 * Note:
 *   - This function internally allocates and frees a temporary aligned query buffer.
 *   - Uses ef = 1 for higher layers (greedy search), and `ef_search` at layer 0.
 */
int graph_knn_search(IndexHNSW *idx, float32_t *vector, Heap *R, int k) {
    SearchContext sc;
    GraphNode *ep;
    Heap W = HEAP_INIT();
    HeapNode w;
    int i, ret = SYSTEM_ERROR, ef;

    PANIC_IF(heap_cap(R) != k, "incorrect space allocation in R");

    sc.query = (float32_t *) aligned_calloc_mem(16, idx->dims_aligned * sizeof(float32_t));
    if (!sc.query) 
        return SYSTEM_ERROR;

    memcpy(sc.query, vector, idx->dims_aligned*sizeof(float32_t));
    sc.cmp = idx->cmp;
    sc.dims_aligned   = idx->dims_aligned;
	sc.filter_alive = 0;
    ep = idx->gentry;
    for (i = idx->top_level; i > 0; i--) {
        if (search_layer(&sc, &ep, 1, 1, i, &W) != SUCCESS)
            goto return_with_error;
    
        PANIC_IF(heap_size(&W) != 1, "assertion in search layer");
        PANIC_IF(heap_pop(&W, &w) != HEAP_SUCCESS, "invalid pop");
        ep = (GraphNode *) HEAP_NODE_PTR(w);
        heap_destroy(&W);
    }
	ef = k > idx->ef_search ? k * 2 : idx->ef_search;
	// Agregar si filtro, agregar si tiene en cuenta borrados
    
	sc.filter_alive = 1;
	if (search_layer(&sc, &ep,1, ef, 0, &W) != SUCCESS)
        goto return_with_error;

    if (select_neighbors(&sc, &W, k, 0, 0) != SUCCESS)
        goto return_with_error;

    PANIC_IF(heap_size(&W) > k, "invalid heap size");

    for (int n = heap_size(&W); n > 0; n = heap_size(&W)) {
        PANIC_IF(heap_pop(&W, &w) != HEAP_SUCCESS, "invalid condition");
        PANIC_IF(heap_insert(R, &w) != HEAP_SUCCESS, "invalid condition");
    }
    ret = SUCCESS;

return_with_error:
    free_aligned_mem(sc.query);
    heap_destroy(&W);
    return ret;
}


#include "index.h"
#include "graph.h"
#include "heap.h"
#include "map.h"

/**
 * @brief Initializes the random number generator seed.
 *
 * Uses the current system time to seed the standard rand() function.
 * This should be called once at the start of the program to ensure
 * non-reproducible randomness.
 */
static void init_random_seed() {
    srand((unsigned int) time(NULL));
}

/**
 * @brief Search for the top closest neighbors in a HNSW index.
 *
 * @param index   Pointer to the IndexHNSW structure
 * @param vector  Query vector
 * @param dims    Number of dimensions (must match index)
 * @param result  Output array of MatchResult[n], sorted by ascending distance
 * @param n       Len of MatchResult array
 *
 * @returns ErrorCode (SUCCESS or failure reason)
 */
static int hnsw_search_n(void *index, float32_t *vector, uint16_t dims, MatchResult *result, int n) {
    IndexHNSW *idx = (IndexHNSW *)index;
    Heap R = HEAP_INIT();
    HeapNode r;
    int ret;

    if (dims != idx->dims)
        return INVALID_DIMENSIONS;

    if (init_heap(&R, HEAP_BETTER_TOP, n, idx->cmp->is_better_match)!= HEAP_SUCCESS)
        return SYSTEM_ERROR;
    ret = graph_knn_search(idx, vector, &R, n);
    if (ret == SUCCESS) 
        for (int i = 0; i < n && heap_size(&R) > 0; i++) {
            PANIC_IF(heap_pop(&R, &r) != HEAP_SUCCESS, "error in heap");
            result[i].distance = r.distance;
            result[i].id = ((GraphNode* )HEAP_NODE_PTR(r))->vector->id; 
        }

    heap_destroy(&R);
    return ret;
}

static int hnsw_insert(void *index, uint64_t id, float32_t *vector, uint16_t dims, void **ref) {
    IndexHNSW *idx = (IndexHNSW *)index;
    GraphNode *node;
    
    if (dims != idx->dims)
        return INVALID_DIMENSIONS;
    
    node = alloc_graph_node(id, vector, idx->dims_aligned, idx->M0);
    if (node == NULL)
        return SYSTEM_ERROR;

    if (graph_insert(idx, node) != SUCCESS) {
        free_graph_node(&node);
        return SYSTEM_ERROR;
    }

    *ref = node;
    return SUCCESS;
}

/**
 * @brief Search for the top closest neighbors in a HNSW index.
 *
 * @param index   Pointer to the IndexHNSW structure
 * @param vector  Query vector
 * @param dims    Number of dimensions (must match index)
 * @param result  Output array of MatchResult[n], sorted by ascending distance
 *
 * @returns ErrorCode (SUCCESS or failure reason)
 */
static int hnsw_search(void *index, float32_t *vector, uint16_t dims, MatchResult *result) {
    return hnsw_search_n(index, vector, dims, result, 1);
}

/**
 * @brief Releases all memory associated with the HNSW index.
 *
 * Frees all nodes in the linked list and the index structure itself.
 *
 * @param index A pointer to the index pointer to be released and set to NULL.
 * @return SUCCESS on successful release.
 */
static int hnsw_release(void **index) {
    IndexHNSW *idx = (IndexHNSW *)*index;
    GraphNode *ptr;

    ptr = idx->lentry;
    while (ptr) {
        idx->lentry = ptr->next;
        idx->elements--;
        free_graph_node(&ptr);
        ptr = idx->lentry;
    }

    free_mem(idx);  
    *index = NULL;
    return SUCCESS;
}

/**
 * @brief Marks a graph node as logically deleted.
 *
 * This function does not physically remove the node from memory or index,
 * it only marks the node as inactive (alive = 0).
 *
 * @param index Pointer to the index (unused but required for signature).
 * @param ref Pointer to the graph node to mark as deleted.
 * @return SUCCESS if the node is marked successfully, INVALID_INDEX if index is NULL.
 */
static int hnsw_delete(void *index, void *ref) {
    if (!index) return INVALID_INDEX;
    GraphNode *ptr = (GraphNode *) ref;	
    ptr->alive = 0;
    return SUCCESS;
}

/**
 * @brief Initializes a new HNSW index structure.
 *
 * Allocates and configures a new HNSW index with optional tuning parameters.
 * If no context is provided, default parameters are used.
 *
 * @param method Comparison method identifier (e.g., L2, cosine, dot).
 * @param dims Number of dimensions in the input vectors.
 * @param context Pointer to an HNSWContext structure with configuration parameters,
 *                or NULL to use default values.
 * @return Pointer to the newly initialized IndexHNSW, or NULL on failure.
 */
static IndexHNSW *hnsw_init(int method, uint16_t dims, HNSWContext *context) {
    IndexHNSW *index = (IndexHNSW *) calloc_mem(1,sizeof(IndexHNSW));
    if (index == NULL)
        return NULL;

    init_random_seed();
    index->cmp = get_method(method);
    if (!index->cmp) {
        free_mem(index);
        return NULL;
    }
    index->gentry = NULL;
    index->lentry = NULL;
    index->elements = 0;

    index->dims = dims;
    index->dims_aligned = ALIGN_DIMS(dims);
    if (context == NULL) {
        index->ef_search    = 110;
        index->ef_construct = 220;
        index->M0 = 32;
    } else {
        index->ef_search    = context->ef_search;
        index->ef_construct = context->ef_construct;
        index->M0 = context->M0;
    }
    return index;
}


static int hnsw_remap(void *index, Map *map) {
    IndexHNSW *idx = (IndexHNSW *)index;
    GraphNode *ptr;

    ptr = idx->lentry;
    
    while (ptr) {
        if (ptr->alive && ptr->vector)
            if (map_insert_p(map, ptr->vector->id, ptr) != MAP_SUCCESS)
                return SYSTEM_ERROR;
        ptr = ptr->next;
    }
    return SUCCESS;
}

static int hnsw_update_icontext(void *index, void *context, int mode) {
    IndexHNSW   *idx = (IndexHNSW *) index;
    HNSWContext *ctx = (HNSWContext *) context;

    if (mode & HNSW_CONTEXT) {
        if (mode & HNSW_CONTEXT_SET_EF_CONSTRUCT)
            idx->ef_construct = ctx->ef_construct;
        if (mode & HNSW_CONTEXT_SET_EF_SEARCH)
            idx->ef_search = ctx->ef_search;
        if (mode & HNSW_CONTEXT_SET_M0)
            idx->M0 = ctx->M0;
    }
    return SUCCESS;
}


static inline void hnsw_functions(Index *idx) {
    idx->search   = hnsw_search;
    idx->search_n = hnsw_search_n;
    idx->insert   = hnsw_insert;
    idx->dump     = NULL;
    idx->remap    = hnsw_remap;
    idx->delete   = hnsw_delete;
    idx->release  = hnsw_release;
    idx->update_icontext = hnsw_update_icontext;
}

int hnsw_index(Index *idx, int method, uint16_t dims, HNSWContext *context) {
    idx->data = hnsw_init(method, dims, context);
    if (idx->data == NULL) 
        return SYSTEM_ERROR;
    idx->name     = "hnsw";
    hnsw_functions(idx);
    return SUCCESS;
}
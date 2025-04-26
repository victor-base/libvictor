#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "vector.h"
#include "panic.h"
#include "heap.h"
#include "method.h"
#include "index.h"
#include "store.h"
#include "map.h"
#include "index_nsw.h"
#include "mem.h"


typedef struct {
    Heap W;
    Heap C;
    Map  visited;
    int  k;
} SearchContext;

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

/**
 * Computes the optimal out-degree (number of neighbors) for a node
 * based on the current number of elements in the index `N`.
 *
 * This function uses a precomputed table (`odegree_table[]`) indexed
 * by the most significant bit (MSB) of `N`, adjusted by an offset,
 * to produce a scalable and logarithmic growth in degree.
 *
 * Behavior:
 *  - For small values of `N` (< 16), the minimum degree is returned.
 *  - For large values, the output is clamped to the last entry in the table.
 *
 * Parameters:
 *  - N: Total number of elements currently in the index.
 *
 * Returns:
 *  - An appropriate out-degree value based on growth heuristics.
 */
static inline int compute_odegree(uint64_t N) {
    if (N < 16) return odegree_table[0];
    int index = get_msb_index(N) - 4;  // Offset from 2^4 = 16
    if (index < 0) return odegree_table[0];
    if (index >= (int)(sizeof(odegree_table))) return odegree_table[sizeof(odegree_table) - 1];
    return odegree_table[index];
}

/**
 * Computes the `ef_construction` value for the insertion process in NSW/HNSW.
 *
 * This value determines how broadly the graph will be explored when inserting
 * a new node. A higher `ef_construction` generally leads to better connectivity
 * and quality at the cost of more computation.
 *
 * The function adapts based on the index size `N`, using a power-law function,
 * and ensures a minimum baseline of `3 * M`.
 *
 * Parameters:
 *  - N: Number of elements in the graph.
 *  - M: Maximum out-degree (connectivity constraint).
 *
 * Returns:
 *  - A dynamically tuned `ef_construction` value, guaranteed to be ≥ 3 * M.
 */

static int compute_ef_construction(uint64_t N, int M) {
    double base = pow((double)N, 0.28);
    int ef = (int)ceil(base);
    int min_ef = 3 * M;
    return ef > min_ef ? ef : min_ef;
}

/**
 * Computes the `ef_search` value for querying the NSW index.
 *
 * This parameter controls the search breadth — how many candidates
 * are kept during traversal. A higher value generally improves recall
 * but increases latency.
 *
 * The function uses a power-law heuristic based on index size `N` and
 * ensures two lower bounds:
 *  - At least `2 * M`, where M is the max out-degree.
 *  - At least `4 * k`, where k is the number of final results requested.
 *
 * Parameters:
 *  - N: Number of nodes in the index.
 *  - M: Out-degree configuration.
 *  - k: Number of nearest neighbors to retrieve.
 *
 * Returns:
 *  - A tuned `ef_search` value that balances recall and efficiency.
 */

static int compute_ef_search(uint64_t N, int M, int k) {
    double base = pow((double)N, 0.35);
    int ef = (int)ceil(base);
    int min_ef = 2 * M;
    int min_recall = 4 * k;

    if (ef < min_ef) ef = min_ef;
    if (ef < min_recall) ef = min_recall;
    return ef;
}




/*-------------------------------------------------------------------------------------*
 *                                PRIVATE FUNCTIONS                                    *
 *-------------------------------------------------------------------------------------*/


/**
 * Serializes an in-memory NSW node into its on-disk representation.
 *
 * This function transforms an `INodeNSW` structure into a compact `SNodeNSW`
 * format suitable for persistent storage. It translates internal memory pointers
 * (`Vector*` and neighbor `INodeNSW*`) into file offsets using the provided maps.
 * 
 * The output node is padded with zeroed neighbor slots if the current out-degree
 * is less than `max_neighbors`, ensuring fixed-size storage on disk.
 *
 * @param dst - Pointer to the destination SNodeNSW (on-disk format).
 * @param src - Pointer to the source in-memory INodeNSW node.
 * @param max_neighbors - Maximum number of neighbors (allocated slots in `neighbors[]`).
 * @param io - IO Context struct
 */
static void nsw_inode2snode(SNodeNSW *dst, INodeNSW *src, int max_neighbors, IOContext *io) {
    int i;
    uint64_t iv;
    uint64_t in;

    PANIC_IF(src->odegree > max_neighbors, "odegree exceeds max_neighbors");


    PANIC_IF(
        map_get_safe(&io->vat, (uint64_t)(uintptr_t) src->vector, &iv) == MAP_KEY_NOT_FOUND, 
        "invalid vector reference in node"
    );
    dst->vector  = iv;
    dst->odegree = (uint8_t) src->odegree;
    dst->alive   = (uint8_t) src->alive;
    for (i = 0; i < src->odegree; i++) {
        PANIC_IF(
            map_get_safe(&io->nat, (uint64_t)(uintptr_t) src->neighbors[i], &in) == MAP_KEY_NOT_FOUND, 
            "invalid neighbor reference in node"
        );
        dst->neighbors[i] = in;
    }
    for ( ; i < max_neighbors; i++) {
        dst->neighbors[i] = 0;
    }
}

/**
 * Reconstructs an in-memory NSW node from its serialized (on-disk) representation.
 *
 * This function transforms an `SNodeNSW` structure (read from disk) back into
 * a fully connected `INodeNSW` structure in memory. It uses the `vat` and `nat`
 * maps to resolve disk offsets back to their corresponding memory pointers.
 *
 * The function also increments the `idegree` of each neighbor node referenced,
 * restoring inbound degree metadata used for graph consistency.
 *
 * Neighbor slots beyond the actual out-degree are set to NULL to ensure clean state.
 *
 * @param dst - Pointer to the destination in-memory INodeNSW.
 * @param src - Pointer to the source SNodeNSW loaded from disk.
 * @param max_neighbors - Maximum number of neighbors (allocated slots in `neighbors[]`).
 * @param io - IO Context struct
 */
static int nsw_snode2inode(INodeNSW *dst, SNodeNSW *src, int max_neighbors, IOContext *io, INodeNSW **inodes) {
    int i;
    dst->vector =  src->vector < io->elements ? (Vector *) io->vectors[src->vector] : NULL;
    if (dst->vector == NULL)
        return INVALID_FILE;
    dst->odegree = (int) src->odegree;
    dst->alive   = (int) src->alive;
    for ( i = 0; i < src->odegree; i++) {
        dst->neighbors[i] = src->neighbors[i] < io->elements ? inodes[src->neighbors[i]] : NULL;
        if (dst->neighbors[i] == NULL)
            return INVALID_FILE;
        dst->neighbors[i]->idegree++;
    }
    for ( ;i < max_neighbors; i++) {
        dst->neighbors[i] = NULL;
    }
    return SUCCESS;
}


/**
 * Dumps the entire NSW index to a binary file on disk.
 *
 * This function serializes an `IndexNSW` structure into a compact on-disk
 * representation composed of three sections:
 * 1. A header (`SIHdrNSW`)
 * 2. A vector section (contiguous `Vector` entries)
 * 3. A node section (contiguous `SNodeNSW` entries)
 *
 * The dumping process consists de two passes:
 * - First pass: traverse the graph, write all vectors, and collect pointer-to-offset mappings.
 * - Second pass: serialize each node using the offset maps and write them sequentially.
 *
 * At the end, a compact header is written at the start of the file, containing offsets and metadata
 * required to reconstruct the graph via `nsw_load()`.
 *
 * @param index - Pointer to the in-memory NSW index to serialize.
 * @param filename - Path to the output file to write the dump.
 *
 * @return SUCCESS if the dump was successful, or SYSTEM_ERROR on failure.
 */
int nsw_dump(void *idx, IOContext *io) {
    IndexNSW *index = (IndexNSW *) idx;
    SNodeNSW *node;
    INodeNSW *entry;
    int ret = SUCCESS, i;

    if (io_init(io, index->elements, sizeof(SIHdrNSW), 1) != SUCCESS)
        return SYSTEM_ERROR;
    
    io->nsize = SNODESZ(index->odegree_hl);
    io->vsize = VECTORSZ(index->dims_aligned);
    io->hsize = sizeof(SIHdrNSW);
    io->dims = index->dims;
    io->dims_aligned = index->dims_aligned;
    io->elements = index->elements;
    io->itype = NSW_INDEX;
    io->method = index->cmp->type;
    

    entry = index->lentry;
    for (i = 0; entry; entry = entry->next, i++) {
        PANIC_IF(i >= (int) io->elements, "index overflow while mapping entries");
        if (map_insert(&io->nat, (uintptr_t)entry, i) != MAP_SUCCESS) {
            ret = SYSTEM_ERROR;
            goto cleanup;
        }

        io->vectors[i] = entry->vector;
        if (map_insert(&io->vat, (uintptr_t)entry->vector, i) != MAP_SUCCESS) {
            ret = SYSTEM_ERROR;
            goto cleanup;
        }
    }

    entry = index->lentry;    
    for (i = 0; entry; entry = entry->next, i++) {
        PANIC_IF(i >= (int) io->elements, "index overflow while mapping entries");
        node = (SNodeNSW *) calloc_mem(1, io->nsize);
        if (!node) {
            ret = SYSTEM_ERROR;
            goto cleanup;
        }
        nsw_inode2snode(node, entry, index->odegree_hl, io);
        io->nodes[i] = node;
    }

    ((SIHdrNSW *)io->header)->ef_construct = index->ef_construct;
    ((SIHdrNSW *)io->header)->ef_search = index->ef_search;
    ((SIHdrNSW *)io->header)->odegree_computed = index->odegree_computed;
    ((SIHdrNSW *)io->header)->odegree_hl = index->odegree_hl;
    ((SIHdrNSW *)io->header)->odegree_sl = index->odegree_sl;
    
    uint64_t gptr;
    PANIC_IF(
        map_get_safe(&io->nat, (uint64_t)(uintptr_t) index->gentry, &gptr) == MAP_KEY_NOT_FOUND, 
        "invalid gentry reference in map"
    );

    ((SIHdrNSW *)io->header)->entry = gptr;

cleanup:
    if (ret != SUCCESS)
        io_free(io);
    return ret;
}



IndexNSW *nsw_load(IOContext *io) {
    IndexNSW *idx = NULL; 
    INodeNSW **inodes;
    INodeNSW *entry;
    SIHdrNSW *hdr = io->header;

    inodes = calloc_mem(io->elements, sizeof(INodeNSW *));
    if (inodes == NULL)
        return NULL;

    entry = NULL;
    for (int i = 0; i < (int) io->elements; i++) {
        if ((inodes[i] = calloc_mem(1, NSW_NODESZ(hdr->odegree_hl))) == NULL) {
            while (i && i-- >= 0) {
                free_mem(inodes[i]);
                inodes[i] = NULL;
            }
            free_mem(inodes);
            return NULL;
        }
        inodes[i]->next = entry;
        entry = inodes[i];
    } 
    
    for (int i = 0; i < (int)io->elements; i++)
        if (nsw_snode2inode(inodes[i], io->nodes[i], hdr->odegree_hl, io, inodes) != SUCCESS)
            goto error;

    idx = (IndexNSW *) calloc_mem(1, sizeof(IndexNSW));
    if (!idx) 
        goto error;

    idx->cmp = get_method(io->method);
    idx->dims = io->dims;
    idx->dims_aligned = io->dims_aligned;
    idx->ef_construct = hdr->ef_construct;
    idx->ef_search = hdr->ef_search;
    idx->elements  = io->elements;
    idx->odegree_computed = hdr->odegree_computed;
    idx->odegree_hl = hdr->odegree_hl;
    idx->odegree_sl = hdr->odegree_sl;
    idx->lentry = entry;
    idx->gentry = inodes[hdr->entry];
    return idx;

error:
    for (int i = 0; i < (int)io->elements; i++) {
        free_mem(inodes[i]);
        inodes[i] = NULL;
    }
    free_mem(inodes);
    return NULL;
}
/**
 * Initializes a new NSW (Navigable Small World) index structure with the given
 * dimensionality and configuration context.
 *
 * This function allocates and configures a fresh instance of `IndexNSW`, setting
 * its comparison method, aligned dimensionality, exploration parameters, and
 * degree constraints based on either a provided context or default heuristics.
 *
 * Behavior:
 *  - If `context` is NULL, `ef_search` and `ef_construct` will be set to EF_AUTOTUNED.
 *  - `odegree_sl` is computed progressively if context is missing or explicitly
 *    set to `OD_PROGESIVE`, otherwise it uses a fixed hard limit (`HARDLIMIT_M`).
 *  - The comparison method is selected using the integer `method` parameter,
 *    which maps to a supported distance metric (e.g., L2, cosine).
 *
 * Parameters:
 *  - method: Integer identifier of the distance comparison method.
 *  - dims: Dimensionality of the vectors to be stored in the index.
 *  - context: Optional pointer to an `NSWContext` configuration structure. If NULL,
 *             default values and progressive heuristics are used.
 *
 * Returns:
 *  - A pointer to an initialized `IndexNSW` instance on success.
 *  - NULL if allocation or comparison method initialization fails.
 */

static IndexNSW *nsw_init(int method, uint16_t dims, NSWContext *context) {
    IndexNSW *index = (IndexNSW *) calloc_mem(1,sizeof(IndexNSW));
    if (index == NULL)
        return NULL;

    index->cmp = get_method(method);
    if (!index->cmp) {
        free_mem(index);
        return NULL;
    }
    index->gentry = NULL;
    index->lentry = NULL;
    index->elements = 0;
    index->odegree_computed = 0;

    index->dims = dims;
    index->dims_aligned = ALIGN_DIMS(dims);
    index->ef_search   = context == NULL ? EF_AUTOTUNED : context->ef_search;
    index->ef_construct = context == NULL ? EF_AUTOTUNED : context->ef_construct;
    index->odegree_hl = HARDLIMIT_M;
    if (context == NULL || context->odegree == OD_PROGESIVE) {
        index->odegree_computed = 1;
        index->odegree_sl = compute_odegree(0);
    } else {
        index->odegree_sl = context->odegree;
    }
    return index;
}



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
    HeapNode c_node;
    HeapNode w_node;
    HeapNode n_node;
    int ret, i;
   
    ret = map_insert_p(&sc->visited, entry->vector->id, NULL);
    if (ret != MAP_SUCCESS) 
        return SYSTEM_ERROR;

    n_node.distance = cmp->compare_vectors(v, entry->vector->vector, dims_aligned);
    HEAP_NODE_PTR(n_node) = (INodeNSW *)entry;

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
        
        current = (INodeNSW *) HEAP_NODE_PTR(c_node);
        for (i = 0; i < current->odegree; i++) {
            neighbor = current->neighbors[i];
            if (neighbor && neighbor->vector && !map_has(&sc->visited, neighbor->vector->id)) {
                ret = map_insert_p(&sc->visited, neighbor->vector->id, NULL);
                if (ret != MAP_SUCCESS) 
                    return SYSTEM_ERROR;

                if (!neighbor->alive)
                    continue;

                c_node.distance = cmp->compare_vectors(v, neighbor->vector->vector, dims_aligned);
                HEAP_NODE_PTR(c_node) = neighbor;
                
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
 * Performs a nearest neighbor search over the NSW (Navigable Small World) index,
 * returning the top `n` closest results to a given input vector.
 *
 * The search begins from the global entry point (`gentry`) and explores the graph
 * using a best-first strategy. The breadth of exploration is controlled by the
 * `ef_search` parameter (either fixed or auto-tuned). Results are written to the
 * provided `result` array in descending priority (i.e., `result[0]` holds the
 * farthest among the top-n matches).
 *
 * The query vector is first copied into aligned memory (based on `dims_aligned`)
 * to optimize distance computations on SIMD-capable architectures.
 *
 * Parameters:
 *  - index: Pointer to the IndexNSW structure.
 *  - vector: Pointer to the input query vector.
 *  - dims: Dimensionality of the query vector.
 *  - result: Pointer to a pre-allocated array of size `n` for storing results.
 *  - n: Number of nearest neighbors to return (top-n).
 *
 * Returns:
 *  - SUCCESS if the search completed correctly.
 *  - INVALID_DIMENSIONS if the vector dimensionality mismatches the index.
 *  - INDEX_EMPTY if the graph contains no elements.
 *  - SYSTEM_ERROR on memory allocation or context initialization failure.
 *  - Any error code propagated from `nsw_explore()` if exploration fails.
 *
 */
static int nsw_search_n(void *index, float32_t *vector, uint16_t dims, MatchResult *result, int n) {
    IndexNSW *idx = (IndexNSW *) index;
    INodeNSW *entry;
    SearchContext sc;
    float32_t *v;
    int ef, ret;

    if (dims != idx->dims) 
        return INVALID_DIMENSIONS;

    entry = idx->gentry;
    if (entry == NULL)
        return INDEX_EMPTY;

    v = (float32_t *) aligned_calloc_mem(16, idx->dims_aligned * sizeof(float32_t));
    if (v == NULL)
        return SYSTEM_ERROR;

    memcpy(v, vector, dims * sizeof(float32_t));

    if (idx->ef_search == EF_AUTOTUNED)
        ef = compute_ef_search(idx->elements, idx->odegree_sl, n);
    else
        ef = idx->ef_search;
    if (init_search_context(&sc, ef, n, idx->cmp->is_better_match) != SUCCESS) {
        free_aligned_mem(v);
        return SYSTEM_ERROR;
    }
        
    if ((ret = nsw_explore(entry,&sc,v,idx->dims_aligned, idx->cmp)) == SUCCESS)
        for (int k = heap_size(&sc.W); k > 0; k = heap_size(&sc.W)) {
            HeapNode node;
            PANIC_IF(heap_pop(&sc.W, &node) == HEAP_ERROR_EMPTY, "lack of consistency");
            result[k-1].distance = node.distance;
            result[k-1].id = ((INodeNSW *)HEAP_NODE_PTR(node))->vector->id;
        }

    destroy_search_context(&sc);
    free_aligned_mem(v);
    return ret;
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


/*
 * nsw_release - Releases all resources associated with a flat index.
 *
 * @param index - Pointer to the index to be released.
 *
 * @return SUCCESS on success, INVALID_INDEX if index is NULL.
 */
static int nsw_release(void **index) {
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


static int nsw_delete(void *index, void *ref) {
    if (!index) return INVALID_INDEX;
    INodeNSW *ptr = (INodeNSW *) ref;	
    ptr->alive = 0;
    return SUCCESS;
}

static int nsw_remap(void *index, Map *map) {
    IndexNSW *idx = (IndexNSW *)index;
    INodeNSW *ptr;

    ptr = idx->lentry;
    
    while (ptr) {
        if (ptr->alive && ptr->vector)
            if (map_insert_p(map, ptr->vector->id, ptr) != MAP_SUCCESS)
                return SYSTEM_ERROR;
        ptr = ptr->next;
    }
    return SUCCESS;
}

/**
 * Allocates and initializes a new INodeNSW structure with space for the specified
 * maximum number of out-neighbors (`odegree_max`). This function also creates and
 * assigns the internal vector representation associated with the node.
 *
 * The node is allocated with memory sized dynamically based on the neighbor capacity,
 * using the `NSW_NODESZ(odegree_max)` macro, which is expected to calculate the full
 * memory footprint needed for a node with `odegree_max` neighbor slots.
 *
 * If either the node memory allocation or the vector initialization fails, the function
 * ensures all resources are properly freed before returning NULL.
 *
 * Parameters:
 *  - id: the unique identifier associated with this node and its vector.
 *  - vector: pointer to the vector data to be copied or referenced.
 *  - dims: dimensionality of the vector.
 *  - odegree_max: maximum number of neighbors the node can hold (capacity).
 *
 * Returns:
 *  - A pointer to a fully initialized INodeNSW instance on success.
 *  - NULL if memory allocation or vector creation fails.
 */

INodeNSW *make_inodensw(uint64_t id, float32_t *vector, uint16_t dims, int odegree_max) {
    INodeNSW *node = (INodeNSW *) calloc_mem(1, NSW_NODESZ(odegree_max));

    if (node == NULL)
        return NULL;

    node->alive  = 1;
    node->vector = make_vector(id, vector, dims);
    if (node->vector == NULL) {
        free_mem(node);
        return NULL;
    }	

    return node;
}

/**
 * Finds the index of the worst (least relevant) neighbor currently connected to
 * the given node, based on the configured distance comparison method.
 *
 * This function iterates over the node's active out-neighbors (`odegree`) and
 * uses the provided comparison method (`cmp`) to determine the least favorable
 * connection according to the graph's distance metric. The "worst" neighbor is
 * defined as the one that produces the largest distance value, or equivalently,
 * the least desirable match (as determined by `cmp->is_better_match()`).
 *
 * The function is used in situations where a node's neighbor list is full, and a
 * new candidate might replace the worst current neighbor if it represents a better match.
 *
 * Parameters:
 *  - node: the node whose neighbors are to be evaluated.
 *  - cmp: pointer to the distance comparison method (including metric and match rules).
 *  - dims_aligned: the aligned dimensionality of the vectors, used for comparison.
 *
 * Returns:
 *  - The index (0-based) of the worst neighbor in the `node->neighbors` array.
 *  - -1 if no valid neighbor is found (e.g., all entries are NULL).
 */

 static int nsw_worst_neighbor(INodeNSW *node, CmpMethod *cmp, uint16_t dims_aligned) {
    INodeNSW *cantidate;
    float32_t distance, wd = cmp->worst_match_value;
    int wi = -1, i;

    for (i = 0; i < node->odegree; i ++) {
        cantidate = node->neighbors[i];
        if (cantidate) {
            distance = cmp->compare_vectors(
                node->vector->vector, cantidate->vector->vector, dims_aligned
            );
            if (wi == -1 || !cmp->is_better_match(distance, wd)) {
                wd = distance;
                wi = i;
            }
        }
    }
    return wi;
}


/**
 * Establishes a directed connection from `node` to `neighbor`, and optionally 
 * performs a backward connection (backlink) from `neighbor` to `node` if the 
 * `backlink` flag is set.
 *
 * If `neighbor` has not yet reached its out-degree limit (`odegree_sl`), the backlink
 * is added directly. Otherwise, the function checks whether `node` is a better match 
 * than `neighbor`'s current worst neighbor. If so, and the worst neighbor has an 
 * in-degree greater than 1 (to avoid isolation), it is replaced with `node`, and 
 * degree counters are updated accordingly.
 *
 * This logic enforces conditional symmetry in the graph without violating per-node 
 * connection limits, and ensures that no node is disconnected as a result of an 
 * aggressive replacement.
 *
 * Parameters:
 *  - idx: the index configuration and comparator functions.
 *  - node: the source node initiating the connection.
 *  - neighbor: the destination node to be connected to.
 *  - backlink: if non-zero, attempt to create a reverse connection.
 *
 * Returns:
 *  - SUCCESS if the connection (and backlink, if applicable) was successful.
 *  - -1 if the backlink could not be established due to structural constraints.
 */

 static int nsw_connect_to(const IndexNSW *const idx, INodeNSW *node, INodeNSW *neighbor, int backlink) {
    INodeNSW *worst;
    float32_t d1, d2;
    int wi;
    node->neighbors[node->odegree++] = neighbor;
    neighbor->idegree++;
    if (!backlink)
        return SUCCESS;
    
    if (neighbor->odegree < idx->odegree_sl) {
        neighbor->neighbors[neighbor->odegree++] = node;
        node->idegree++;
        return SUCCESS;
    }

    wi = nsw_worst_neighbor(neighbor, idx->cmp, idx->dims_aligned);
    if (wi == -1)
        return -1;

    worst = neighbor->neighbors[wi];
    d1 = idx->cmp->compare_vectors(neighbor->vector->vector, worst->vector->vector, idx->dims_aligned);
    d2 = idx->cmp->compare_vectors(node->vector->vector, neighbor->vector->vector, idx->dims_aligned);

    if (idx->cmp->is_better_match(d2, d1) && worst->idegree > 1) {
        worst->idegree--;
        node->idegree++;
        neighbor->neighbors[wi] = node;
        return SUCCESS;
    }
    return -1;
}   


/**
 * Inserts a new vector into the NSW (Navigable Small World) graph index.
 * 
 * This function handles the full insertion pipeline:
 *  - It validates the dimensionality of the input vector.
 *  - It creates a new graph node with storage for the vector and neighbor slots.
 *  - If the index is empty, it initializes the graph entry points.
 *  - Otherwise, it searches for the closest existing nodes using the current entry point
 *    and the `ef_construction` parameter to control the exploration breadth.
 *  - It connects the new node to the top-k nearest neighbors discovered, applying
 *    symmetric connections when allowed via `nsw_connect_to()`.
 *
 * The function ensures proper memory cleanup in case of any failure, and maintains
 * the `ref` pointer to the newly created node if the insertion succeeds.
 *
 * Parameters:
 *  - index: Pointer to the IndexNSW structure (cast internally).
 *  - id: Unique identifier for the vector.
 *  - vector: Pointer to the input float vector data.
 *  - dims: Dimensionality of the input vector.
 *  - ref: Output pointer to store a reference to the newly inserted node.
 *
 * Returns:
 *  - SUCCESS (typically 0) if the insertion completes correctly.
 *  - INVALID_DIMENSIONS if the input vector's dimensionality is incorrect.
 *  - SYSTEM_ERROR if node allocation or search context initialization fails.
 *  - Any other propagated error returned from `nsw_explore()` if search fails.
 */

static int nsw_insert(void *index, uint64_t id, float32_t *vector, uint16_t dims, void **ref) {
    SearchContext sc;
    IndexNSW *idx = (IndexNSW *) index;
    INodeNSW *node;
    int ef, ret;
    
    if (dims != idx->dims)
        return INVALID_DIMENSIONS;

    node = make_inodensw(id, vector, dims, HARDLIMIT_M);
    if (!node) return SYSTEM_ERROR;

    if (idx->elements == 0) {
        idx->lentry = node;
        idx->gentry = node;
        idx->elements++;
        *ref = node;
        return SUCCESS;
    }
    node->next = idx->lentry;
    idx->lentry = node;

    if (idx->ef_construct == EF_AUTOTUNED)
        ef = compute_ef_construction(idx->elements, idx->odegree_sl);
    else
        ef = idx->ef_construct;

    if (init_search_context(&sc, ef, idx->odegree_sl, idx->cmp->is_better_match) != SUCCESS) {
        free_vector(&(node->vector));
        free_mem(node);
        return SYSTEM_ERROR;
    }

    if ((ret = nsw_explore(idx->gentry, &sc, node->vector->vector, idx->dims_aligned, idx->cmp)) == SUCCESS) {
        for (int k = heap_size(&sc.W); k > 0; k = heap_size(&sc.W)) {
            INodeNSW *neighbor;
            HeapNode candidate;
            PANIC_IF(heap_pop(&sc.W, &candidate) == HEAP_ERROR_EMPTY, "lack of consistency");
            neighbor = (INodeNSW *) HEAP_NODE_PTR(candidate);
            nsw_connect_to(idx, node, neighbor, 1);
            
        }
        idx->elements++;
    } else {
        free_vector(&(node->vector));
        free_mem(node);
    }
    if (idx->odegree_computed)
        idx->odegree_sl = compute_odegree(idx->elements);
    destroy_search_context(&sc);
    return ret;
}






/*-------------------------------------------------------------------------------------*
 *                                PUBLIC FUNCTIONS                                     *
 *-------------------------------------------------------------------------------------*/

/**
 * Initializes a generic `Index` structure using the NSW (Navigable Small World)
 * implementation as its backend.
 *
 * This function configures the function pointers and internal state of the
 * `Index` abstraction to point to the NSW-specific methods and data structures.
 * It delegates the internal memory and configuration setup to `nsw_init()`.
 *
 * On failure to initialize the internal NSW index, the outer `Index` structure
 * is cleaned up to avoid memory leaks.
 *
 * Parameters:
 *  - idx: Pointer to the high-level `Index` structure to initialize.
 *  - method: Integer identifier for the vector comparison method (e.g., L2, cosine).
 *  - dims: Dimensionality of the vectors the index will store.
 *  - context: Optional configuration parameters for NSW behavior (can be NULL).
 *
 * Returns:
 *  - SUCCESS if the index is successfully initialized.
 *  - SYSTEM_ERROR if memory allocation or NSW setup fails.
 */

static inline void nsw_functions(Index *idx) {
    idx->search   = nsw_search;
    idx->search_n = nsw_search_n;
    idx->insert   = nsw_insert;
    idx->dump     = nsw_dump;
    idx->delete   = nsw_delete;
    idx->remap    = nsw_remap;
    idx->release  = nsw_release;
}

int nsw_index(Index *idx, int method, uint16_t dims, NSWContext *context) {
    idx->data = nsw_init(method, dims, context);
    if (idx->data == NULL) 
        return SYSTEM_ERROR;
    idx->name     = "nsw";
    idx->context  = context;
    nsw_functions(idx);
    return SUCCESS;
}

int nsw_index_load(Index *idx, IOContext *io) {
    idx->data = nsw_load(io);
    if (idx->data == NULL) 
        return SYSTEM_ERROR;
    idx->name     = "nsw";
    idx->context  = NULL;
    nsw_functions(idx);
    return SUCCESS;
}

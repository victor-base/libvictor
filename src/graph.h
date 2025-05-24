#ifndef _GRAPH_H
#define _GRAPH_H

#include "method.h"
#include "heap.h"

/**
 * Degrees - Per-level degree counters for a GraphNode.
 *
 * Stores the number of inbound and outbound edges for a given level.
 * This struct is used to track the degree distribution in HNSW-style graphs.
 *
 * @field idegree  Number of incoming edges at a given level
 * @field odegree  Number of outgoing edges at a given level
 */
typedef struct {
    uint32_t idegree;
    uint32_t odegree;
} Degrees;

/**
 * GraphNode - Represents a node in a multi-level proximity graph (NSW/HNSW).
 *
 * This structure models a single node in the graph. Each node may live across
 * multiple levels (from 0 to `level`), and holds its own vector, degree metadata,
 * and a per-level neighbor array.
 *
 * The structure assumes external memory layout via a single contiguous block.
 * Internal pointers (`degrees`, `neighbors`, and neighbor arrays) must be set up
 * manually during allocation (see `alloc_gnode()`).
 *
 * Fields:
 *   - `vector`: pointer to the vector representation (embedding + ID)
 *   - `level`: highest level this node participates in
 *   - `alive`: 1 if active, 0 if logically deleted (used in soft deletion)
 *   - `degrees`: array of Degrees[L+1], one per level (allocated separately or inline)
 *   - `next`: optional pointer used for flat iteration or linked list chaining
 *   - `neighbors[]`: array of pointers to arrays, each holding the outgoing neighbors
 *                    for a given level (neighbors[0] → level 0, etc.)
 *
 * Flexible design:
 *   - `neighbors[]` is a flexible array of level pointers (GraphNode**[])
 *   - Each `neighbors[l]` is an array of `GraphNode*` with size M0 or M
 *   - Total memory is allocated in a single contiguous block for performance
 */
typedef struct graph_node {
    Vector *vector;
   
    int level;
    int alive;
    Degrees *degrees;
    struct graph_node *next;
    struct graph_node ***neighbors;
} GraphNode;

/* Access per-level degrees */
#define IDEGREE(node, l)         ((node)->degrees[(l)].idegree)
#define ODEGREE(node, l)         ((node)->degrees[(l)].odegree)

/* Access neighbor arrays */
#define NEIGHBOR_LIST(node, l)   ((node)->neighbors[(l)])
#define NEIGHBOR_AT(node, l, i)  ((node)->neighbors[(l)][(i)])

/* Node status */
#define NODE_IS_ALIVE(node)      ((node)->alive != 0)
#define NODE_DELETE(node)        ((node)->alive = 0)
#define NODE_LEVEL(node)         ((node)->level)

/**
 * @struct IndexHNSW
 * @brief Structure representing a Hierarchical Navigable Small World (HNSW) index.
 *
 * Contains all necessary metadata and entry points for performing insertion
 * and search operations using the HNSW algorithm.
 */
typedef struct {
    int ef_search;      /**< Search expansion factor (controls recall vs speed). */
    int ef_construct;   /**< Construction expansion factor (controls graph quality). */
    int M0;             /**< Maximum number of neighbors at level 0. */
    int top_level;      /**< Highest level currently used in the graph. */
    int elements;       /**< Total number of inserted elements. */

    CmpMethod *cmp;         /**< Pointer to the distance comparison function. */
    uint16_t  dims;         /**< Original dimensionality of the vectors. */
    uint16_t  dims_aligned; /**< Aligned dimensionality (e.g., multiple of 4 for SIMD). */
    
    GraphNode *gentry;  /**< Global entry point to the top level of the graph. */
    GraphNode *head;  /**< Local entry list used for traversal or deletion. */
} IndexHNSW;


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

extern GraphNode *alloc_graph_node(uint64_t id, float32_t *vector, uint16_t dims_aligned, int M0);

/**
 * free_gnode - Frees a GraphNode and its associated vector.
 *
 * This function releases both the vector and the node memory.
 * Safe to call with NULL or already-freed nodes.
 *
 * @param g Pointer to a GraphNode pointer. Will be NULL'd after free.
 */
extern void free_graph_node(GraphNode **g);

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
extern int graph_knn_search(IndexHNSW *idx, float32_t *vector, Heap *R, int k);


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
extern int graph_insert(IndexHNSW *idx, GraphNode *node);

#endif

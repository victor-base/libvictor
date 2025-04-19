#ifndef _NSW_H
#define _NSW_H 1

#include "index.h"
#include "vector.h"
#include "method.h"

/**
 * Represents a node in the NSW (Navigable Small World) graph.
 *
 * Each node stores a vector and maintains bidirectional graph information,
 * including incoming and outgoing edges to other nodes. This structure is designed
 * for dynamic graph construction and allows pruning and entry-point promotion
 * strategies based on degree counts.
 *
 * Fields:
 *  - vector: The actual vector data associated with this node.
 *  - idegree: Number of incoming connections (used to avoid isolation).
 *  - odegree: Current number of outgoing connections stored in `neighbors`.
 *  - alive: Flag indicating whether the node is active or logically deleted.
 *  - next: Optional pointer for linking nodes (e.g., for memory pools or linked lists).
 *  - neighbors[]: Flexible array of pointers to outgoing neighbor nodes.
 *                 This array is allocated dynamically based on max degree at creation time.
 */
typedef struct node_nsw {
    Vector *vector;
    int idegree;			
    int odegree;  			

    int alive;
    struct node_nsw *next;
    struct node_nsw *neighbors[];
} INodeNSW;


/**
 * Represents the state and configuration of an NSW (Navigable Small World) graph index.
 *
 * This structure contains global settings and metadata used to guide both insertion
 * and search operations, including degree limits, dimensionality, and search parameters.
 *
 * Fields:
 *  - ef_search: Exploration factor used during search (number of candidates to keep).
 *  - ef_construct: Exploration factor used during insertion (breadth of graph walk).
 *  - odegree_hl: Max out-degree (hard limit) for any node (strict cap).
 *  - odegree_sl: Soft out-degree limit, used during insertion and replacement heuristics.
 *
 *  - cmp: Pointer to the vector comparison method (e.g., L2, cosine).
 *
 *  - elements: Number of elements currently inserted in the index.
 *  - dims: Original number of dimensions of each vector.
 *  - dims_aligned: Aligned dimensionality for SIMD-friendly memory layout (e.g., padded to 4/8/16).
 *
 *  - gentry: Global entry point to the graph (usually the first or farthest inserted node).
 *  - lentry: Local entry point (could be replaced periodically for insertion heuristics).
 */
typedef struct {
    int ef_search;
    int ef_construct;
	int odegree_computed;
    int odegree_hl;
    int odegree_sl;

    CmpMethod *cmp;          

    uint64_t elements;       
    uint16_t dims;           
    uint16_t dims_aligned;
    INodeNSW *gentry;
    INodeNSW *lentry;
} IndexNSW;



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
extern int nsw_index(Index *idx, int method, uint16_t dims, NSWContext *context);

#endif
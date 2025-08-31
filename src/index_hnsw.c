/*
* index_hnsw.c - HNSW Index Implementation for Vector Cache Database
* 
* Copyright (C) 2025 Emiliano A. Billi
*
*
* License:
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <https://www.gnu.org/licenses/>.
*
* Contact: emiliano.billi@gmail.com
*/
#include "index.h"
#include "graph.h"
#include "heap.h"
#include "map.h"

/**
 * @brief Initializes the random seed for the HNSW index.
 *
 * Uses the current system time to seed the random number generator.
 * This function should be called once at the start of the program to ensure randomness.
 */
static void init_random_seed() {
    srand((unsigned int) time(NULL));
}

/**
 * @brief Searches for the top-N closest vectors in the HNSW index with optional tag filtering.
 *
 * @param index  Pointer to the HNSW index.
 * @param tag    Bitmask filter: only vectors whose tag shares at least one bit will be considered.
 *               If tag == 0, no tag filtering is applied.
 * @param vector Pointer to the query vector.
 * @param dims   Number of dimensions of the query vector.
 * @param result Output array of MatchResult to store the best matches.
 * @param n      Number of top matches to return.
 * @return SUCCESS if matches are found, or an error code.
 */
static int hnsw_search(void *index, uint64_t tag, float32_t *vector, uint16_t dims, MatchResult *result, int n) {
    IndexHNSW *idx = (IndexHNSW *)index;
    Heap R = HEAP_INIT();
    HeapNode r;
    int ret;

    if (dims != idx->dims)
        return INVALID_DIMENSIONS;

	if (tag == 0) {
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
	return graph_linear_search(idx, tag, vector, result, n);
}

/**
 * @brief Inserts a new vector into the HNSW index.
 *
 * @param index  Pointer to the HNSW index.
 * @param id     Unique identifier for the vector.
 * @param tag    Tag bitmask for the vector.
 * @param vector Pointer to the vector data.
 * @param dims   Number of dimensions of the vector.
 * @param ref    Output pointer to the inserted node (optional).
 * @return SUCCESS if inserted, or an error code.
 */
static int hnsw_insert(void *index, uint64_t id, uint64_t tag, float32_t *vector, uint16_t dims, void **ref) {
    IndexHNSW *idx = (IndexHNSW *)index;
    GraphNode *node;
    
    if (dims != idx->dims)
        return INVALID_DIMENSIONS;
    
    node = alloc_graph_node(id, tag, vector, idx->dims_aligned, idx->M0);
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
 * @brief Marks a node as deleted in the HNSW index.
 *
 * @param index Pointer to the HNSW index.
 * @param ref   Pointer to the node to be deleted.
 * @return SUCCESS if the node was marked as deleted, INVALID_INDEX on error.
 */
static int hnsw_delete(void *index, void *ref) {
    if (!index) return INVALID_INDEX;
    GraphNode *ptr = (GraphNode *) ref;	
    ptr->alive = 0;
    return SUCCESS;
}


/**
 * @brief Imports vectors from an IOContext into the HNSW index.
 *
 * @param index Pointer to the HNSW index.
 * @param io    IOContext with vectors to import.
 * @param map   Map to register imported nodes.
 * @param mode  Import mode (overwrite, ignore, etc).
 * @return SUCCESS if successful, or an error code.
 */
static int hnsw_import(void *index, IOContext *io, Map *map, int mode) {
	IndexHNSW *idx = (IndexHNSW *)index;
    GraphNode *node;

	if (io->dims != idx->dims || io->dims_aligned != idx->dims_aligned )
        return INVALID_DIMENSIONS;

    for (int i = 0; i < (int) io->elements; i++) {
        if (map_has(map, io->vectors[i]->id)) {
			switch (mode) {

			case IMPORT_OVERWITE:
				PANIC_IF(map_get_safe_p(map, io->vectors[i]->id, (void **)&node) != MAP_SUCCESS, "failed to get existing node");
                PANIC_IF(map_remove_p(map, io->vectors[i]->id) == NULL, "failed to remove duplicate ID from map");
                PANIC_IF(hnsw_delete(index, node) != SUCCESS, "failed to delete existing node");
				node = NULL;
				break;
			case IMPORT_IGNORE_VERBOSE:
				WARNING("hnsw_import", "duplicated entry - ignore");
				continue;
			case IMPORT_IGNORE:
			default:
				continue;
			}

		}
		node = alloc_graph_node(NULL_ID, 0, NULL, 0, idx->M0);
		if (node == NULL)
			return SYSTEM_ERROR;
		
		node->vector = io->vectors[i];
		if (graph_insert(idx, node) != SUCCESS) {
			free_graph_node(&node);
			return SYSTEM_ERROR;
		}
		if (map_insert_p(map, node->vector->id, node) != MAP_SUCCESS)
			return SYSTEM_ERROR;
	
    }
    return SUCCESS;
}


/**
 * @brief Releases all resources associated with the HNSW index.
 *
 * @param index Pointer to the index pointer to be released (set to NULL).
 * @return SUCCESS on success, INVALID_INDEX if index is NULL.
 */
static int hnsw_release(void **index) {
    IndexHNSW *idx = (IndexHNSW *)*index;
    GraphNode *ptr;

    ptr = idx->head;
    while (ptr) {
        idx->head = ptr->next;
        idx->elements--;
        free_graph_node(&ptr);
        ptr = idx->head;
    }

    free_mem(idx);  
    *index = NULL;
    return SUCCESS;
}


/**
 * @brief Initializes a new HNSW index structure.
 *
 * @param method   Comparison method identifier (e.g., L2, cosine, dot).
 * @param dims     Number of dimensions in the input vectors.
 * @param context  Optional configuration parameters (or NULL for defaults).
 * @return Pointer to the initialized index, or NULL on error.
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
    index->head = NULL;
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


/**
 * @brief Rebuilds the ID-to-node map from the linked list in the HNSW index.
 *
 * @param index Pointer to the HNSW index.
 * @param map   Map to fill with live nodes.
 * @return SUCCESS if successful, or SYSTEM_ERROR.
 */
static int hnsw_remap(void *index, Map *map) {
    IndexHNSW *idx = (IndexHNSW *)index;
    GraphNode *ptr;

    ptr = idx->head;
    
    while (ptr) {
        if (ptr->alive && ptr->vector)
            if (map_insert_p(map, ptr->vector->id, ptr) != MAP_SUCCESS)
                return SYSTEM_ERROR;
        ptr = ptr->next;
    }
    return SUCCESS;
}

/**
 * @brief Updates the context parameters of the HNSW index.
 *
 * @param index   Pointer to the HNSW index.
 * @param context Pointer to the context structure with new parameters.
 * @param mode    Bitmask indicating which parameters to update.
 * @return SUCCESS if updated, or an error code.
 */
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

static int hnsw_set_tag(void *index, void *node, uint64_t tag) {
	IndexHNSW *idx = (IndexHNSW *)index;
	GraphNode *n   = (GraphNode *)node;

	if (idx && n && n->vector) {
		n->vector->tag = tag;
	} else 
		return INVALID_REF;
	return SUCCESS;
}

/**
 * @brief Compares a query vector to a node and computes the distance.
 *
 * @param index    Pointer to the HNSW index.
 * @param node     Node to compare.
 * @param vector   Query vector.
 * @param dims     Number of dimensions.
 * @param distance Output: computed distance.
 * @return SUCCESS if valid, or an error code.
 */
static int hnsw_compare(void *index, const void *node, float32_t *vector, uint16_t dims, float32_t *distance) {
	IndexHNSW *idx = (IndexHNSW *)index;
	GraphNode *n   = (GraphNode *)node;
	float32_t *f   = NULL;
	int assigned = 0;
	int ret = SUCCESS;
	if (dims != idx->dims)
		return INVALID_DIMENSIONS;

	if (idx->dims_aligned > dims) {
		f = aligned_calloc_mem(16, idx->dims_aligned);
		memcpy(f, vector, dims);
		assigned = 1;
	} else {
		f = vector;
	}
	
	if (!n->alive) {
		ret = NOT_FOUND_ID;
		*distance = idx->cmp->worst_match_value;
	} else {
		*distance = idx->cmp->compare_vectors(n->vector->vector, f, idx->dims_aligned);
	}
	if (assigned)
		free_aligned_mem(f);
	return ret;
}

__DEFINE_EXPORT_FN(hnsw_export, IndexHNSW, GraphNode)

static inline void hnsw_functions(Index *idx) {
	idx->search   = hnsw_search;
    idx->insert   = hnsw_insert;
    idx->dump     = NULL;
	idx->export   = hnsw_export;
	idx->import   = hnsw_import;
    idx->compare  = hnsw_compare;
	idx->remap    = hnsw_remap;
	idx->set_tag  = hnsw_set_tag;
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
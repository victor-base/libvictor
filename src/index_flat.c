/*
* index_flat.c - Flat Index Implementation for Vector Cache Database
* 
* Copyright (C) 2025 Emiliano A. Billi
*
* This file contains the implementation of a flat index, a simple data structure 
* for storing and retrieving vectors using a doubly linked list. The flat index 
* provides a basic, yet effective, method for nearest neighbor search based on 
* different similarity metrics such as L2 norm and cosine similarity.
*
* Features:
* - Stores vectors in a doubly linked list, allowing dynamic insertions and deletions.
* - Supports different distance metrics through function pointers.
* - Implements read-write locks for thread-safe operations.
* - Provides an interface for searching the closest vector(s).
*
* Limitations:
* - Performs a linear scan, making it inefficient for large-scale datasets.
* - Does not use advanced indexing structures like HNSW or IVF.
* - Best suited for small or medium-sized datasets.
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

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include "iflat_utils.h"
#include "method.h"
#include "index.h"
#include "mem.h"


/*
* IndexFlat - Internal structure for the flat index.
*
* This structure maintains the linked list of stored vectors and metadata 
* required for efficient search operations.
*/
typedef struct {
    CmpMethod *cmp;          // Comparison method (L2 norm, cosine similarity, etc.)
    INodeFlat *head;         // Head of the doubly linked list
    
    uint64_t elements;       // Number of elements stored in the index
    uint16_t dims;           // Number of dimensions for each vector
    uint16_t dims_aligned;   // Aligned dimensions for efficient memory access
} IndexFlat;



/*-------------------------------------------------------------------------------------*
 *                                PRIVATE FUNCTIONS                                    *
 *-------------------------------------------------------------------------------------*/

/*
 * flat_init - Initializes a new flat index.
 *
 * @param method - Distance metric method (e.g., L2NORM, COSINE).
 * @param dims   - Number of dimensions for stored vectors.
 *
 * @return Pointer to the initialized IndexFlat structure or NULL on failure.
 */
static IndexFlat *flat_init(int method, uint16_t dims) {
    IndexFlat *index = (IndexFlat *) calloc_mem(1,sizeof(IndexFlat));
    if (index == NULL)
        return NULL;

    index->cmp = get_method(method);
    if (!index->cmp) {
        free_mem(index);
        return NULL;
    }
    index->head = NULL;
    index->elements = 0;
    index->dims = dims;
    index->dims_aligned = ALIGN_DIMS(dims);
    return index;
}


/*
 * flat_remove - Removes a vector from the flat index.
 *
 * This function searches for a vector with the given ID in the flat index and removes it.
 * The operation ensures thread safety by acquiring a write lock before modifying the index.
 *
 * Steps:
 * 1. Validate the index pointer.
 * 2. Acquire a write lock to prevent concurrent modifications.
 * 3. Call `delete_node` to remove the corresponding node from the linked list.
 * 4. If a node was successfully deleted, decrement the total element count.
 * 5. Release the write lock.
 *
 * @param index - Pointer to the flat index (`IndexFlat`).
 * @param id    - Unique identifier of the vector to be removed.
 *
 * @return SUCCESS if the vector was found and removed.
 *         INVALID_INDEX if the index pointer is NULL.
 *         INVALID_ID if the vector ID was not found in the index.
 */
static int flat_delete(void *index, void *ref) {
    IndexFlat *ptr  = (IndexFlat *)index;
    INodeFlat *node = (INodeFlat *)ref;
    int ret;

    if ((ret = delete_node(&(ptr->head),node)) == SUCCESS) {
        ptr->elements--;
    }
    return ret;
}


/*
 * flat_search_n - Searches for the top-N closest vectors in the flat index.
 *
 * This function performs a nearest neighbor search in a linked list-based
 * flat index, returning the top-N closest matches to the given query vector.
 * 
 * Steps:
 * 1. Validate input parameters.
 * 2. Allocate memory for storing the top-N matches.
 * 3. If necessary, allocate an aligned copy of the query vector.
 * 4. Acquire a read lock to ensure thread safety.
 * 5. Traverse the list and compute distances between stored vectors and the query vector.
 * 6. Maintain a sorted list of the N best matches found.
 * 7. Release the read lock.
 * 8. Free allocated memory if applicable.
 *
 * @param index  - Pointer to the flat index (`IndexFlat`).
 * @param vector - Pointer to the query vector.
 * @param dims   - Number of dimensions of the query vector.
 * @param result - Pointer to a pointer that will store an array of `MatchResult` containing the N best matches.
 * @param n      - Number of top matches to retrieve.
 *
 * @return SUCCESS if matches are found.
 *         INVALID_INDEX if the index pointer is NULL.
 *         INVALID_VECTOR if the vector pointer is NULL.
 *         INVALID_DIMENSIONS if the vector dimensions do not match the index.
 *         INVALID_RESULT if the result pointer is NULL.
 *         SYSTEM_ERROR if memory allocation fails.
 *         INDEX_EMPTY if no elements exist in the index.
 */
static int flat_search_n(void *index, float32_t *vector, uint16_t dims, MatchResult *result, int n) {
    IndexFlat *idx = (IndexFlat *)index;
    INodeFlat *current;
    float32_t *v;
    int ret;

    if (dims != idx->dims) 
        return INVALID_DIMENSIONS;

    if (idx->head == NULL)
        return INDEX_EMPTY;

    v = (float32_t *) aligned_calloc_mem(16, idx->dims_aligned * sizeof(float32_t));
    if (v == NULL)
        return SYSTEM_ERROR;

    memcpy(v, vector, dims * sizeof(float32_t));

    current = idx->head;
    if (current == NULL) {
        ret = INDEX_EMPTY;
    } else {
        ret = flat_linear_search_n(current, v, idx->dims_aligned, result, n, idx->cmp);
    }

    free_aligned_mem(v);
    return ret;
}


/*
 * flat_search - Searches for the best matching vector in the flat index.
 *
 * This function performs a nearest neighbor search in a linked list-based
 * flat index. It finds the vector with the smallest distance (or highest
 * similarity) to the given query vector.
 *
 * Steps:
 * 1. Validate input parameters.
 * 2. Allocate memory for an aligned copy of the input vector if necessary.
 * 3. Initialize the result structure.
 * 4. Acquire a read lock to ensure thread safety.
 * 5. Traverse the list and compute distances between stored vectors and the query vector.
 * 6. Store the best match found.
 * 7. Release the read lock.
 * 8. Free allocated memory if applicable.
 *
 * @param index  - Pointer to the flat index (`IndexFlat`).
 * @param vector - Pointer to the query vector.
 * @param dims   - Number of dimensions of the query vector.
 * @param result - Pointer to `MatchResult`, which will store the best match.
 *
 * @return SUCCESS if a match is found.
 *         INVALID_INDEX if the index pointer is NULL.
 *         INVALID_VECTOR if the vector pointer is NULL.
 *         INVALID_DIMENSIONS if the vector dimensions do not match the index.
 *         INVALID_RESULT if the result pointer is NULL.
 *         SYSTEM_ERROR if memory allocation fails.
 *         INDEX_EMPTY if no elements exist in the index.
 */
static int flat_search(void *index, float32_t *vector, uint16_t dims, MatchResult *result) {
    IndexFlat *idx = (IndexFlat *)index;
    INodeFlat *current;
    float32_t *v;
    int ret;

    if (dims != idx->dims) 
        return INVALID_DIMENSIONS;

    if (idx->head == NULL)
        return INDEX_EMPTY;


    v = (float32_t *) aligned_calloc_mem(16, idx->dims_aligned * sizeof(float32_t));
    if (v == NULL)
        return SYSTEM_ERROR;

    memcpy(v, vector, dims * sizeof(float32_t));

    current = idx->head;
    if (current == NULL) {
        ret = INDEX_EMPTY;
    } else {
        flat_linear_search(current, v, idx->dims_aligned, result, idx->cmp);
        ret = SUCCESS;
    }

    free_aligned_mem(v);
    return ret;
}

/*
 * flat_insert - Inserts a new vector into the flat index.
 *
 * This function inserts a new vector into the linked list-based flat index. 
 * It first validates the input parameters, then allocates memory for a new 
 * node and vector. The function ensures thread safety using a write lock.
 *
 * Steps:
 * 1. Validate input parameters.
 * 2. Allocate memory for a new linked list node (`INodeFlat`).
 * 3. Create a new `Vector` instance that holds the vector data and ID.
 * 4. If allocation fails, return an appropriate error code.
 * 5. Lock the index with a write lock to ensure safe concurrent operations.
 * 6. Insert the new node at the head of the list.
 * 7. Increment the element count in the index.
 * 8. Unlock the write lock.
 *
 * @param index  - Pointer to the flat index structure (`IndexFlat`).
 * @param id     - Unique identifier for the vector.
 * @param vector - Pointer to the vector data.
 * @param dims   - Number of dimensions of the vector.
 *
 * @return SUCCESS if insertion is successful.
 *         INVALID_INDEX if the index pointer is NULL.
 *         INVALID_VECTOR if the vector pointer is NULL.
 *         INVALID_DIMENSIONS if the vector dimensions do not match the index.
 *         SYSTEM_ERROR if memory allocation fails.
 */
static int flat_insert(void *index, uint64_t id, float32_t *vector, uint16_t dims, void **ref) {
    IndexFlat *ptr = (IndexFlat *)index;
    INodeFlat *node;

    if (dims != ptr->dims) 
        return INVALID_DIMENSIONS;

    if ((node = make_inodeflat(id, vector, dims)) == NULL)
        return SYSTEM_ERROR;

    insert_node(&(ptr->head), node);
    ptr->elements++;

    if (ref != NULL)
        *ref = node;

    return SUCCESS;
}

static int flat_remap(void *index, Map *map) {
    IndexFlat *idx = (IndexFlat *)index;
    INodeFlat *ptr;

    ptr = idx->head;
    
    while (ptr) {
        if (ptr->vector)
            if (map_insert_p(map, ptr->vector->id, ptr) != MAP_SUCCESS)
                return SYSTEM_ERROR;
        ptr = ptr->next;
    }
    return SUCCESS;
}

static int flat_compare(void *index, const void *node, float32_t *vector, uint16_t dims, float32_t *distance) {
	IndexFlat *idx = (IndexFlat *)index;
	INodeFlat *n   = (INodeFlat *)node;
	float32_t *f   = NULL;
	int assigned = 0;
	if (dims != idx->dims)
		return INVALID_DIMENSIONS;

	if (idx->dims_aligned > dims) {
		f = aligned_calloc_mem(16, idx->dims_aligned);
		memcpy(f, vector, dims);
		assigned = 1;
	} else {
		f = vector;
	}
	
	*distance = idx->cmp->compare_vectors(n->vector->vector, f, idx->dims_aligned);
	if (assigned)
		free_aligned_mem(f);
	return SUCCESS;
}

__DEFINE_EXPORT_FN(flat_export, IndexFlat, INodeFlat)

/*
 * flat_release - Releases all resources associated with a flat index.
 *
 * @param index - Pointer to the index to be released.
 *
 * @return SUCCESS on success, INVALID_INDEX if index is NULL.
 */
static int flat_release(void **index) {
    IndexFlat *idx = *index;
    INodeFlat *ptr;

    ptr = idx->head;
    while (ptr) {
        idx->head = ptr->next;
        idx->elements--;
        free_vector(&ptr->vector);
        free_mem(ptr);
        ptr = idx->head;
    }

    free_mem(idx);  
    *index = NULL;
    return SUCCESS;
}

static int flat_dump(void *index, IOContext *io) {
    IndexFlat *idx = index;
    INodeFlat *entry = NULL;
    if (io_init(io, idx->elements, 0, IO_INIT_VECTORS) != SUCCESS)
        return SYSTEM_ERROR;

    io->nsize = 0;
    io->vsize = VECTORSZ(idx->dims_aligned);
    io->dims = idx->dims;
    io->dims_aligned = idx->dims_aligned;
    io->itype = FLAT_INDEX;
    io->method = idx->cmp->type;
    io->hsize  = 0;

    entry = idx->head;
    for (int i = 0; entry; entry = entry->next, i++) {
        PANIC_IF(i >= (int) io->elements, "index overflow while mapping entries");
        io->vectors[i] = entry->vector;
    }
    return SUCCESS;
}

static IndexFlat *flat_load(IOContext *io) {
    IndexFlat *index;
    INodeFlat *entry = NULL;


    index = calloc_mem(1, sizeof(IndexFlat));
    if (index == NULL)
        return NULL;

    index->dims = io->dims;
    index->dims_aligned = io->dims_aligned;
    
    index->cmp = get_method(io->method);

    for (int i = 0; i < (int) io->elements; i++) {
        entry = calloc_mem(1, sizeof(INodeFlat));
        if (entry == NULL)
            goto error_return;
        
        entry->vector = io->vectors[i];
        insert_node(&index->head, entry);        
    }
    index->elements = io->elements;
    return index;

error_return:
    entry = index->head;
    while (entry) {
        index->head = entry->next;
        free_mem(entry);
        entry = index->head;    
    }
    free_mem(index);
    return NULL;
}

static int flat_import(void *idx, IOContext *io, Map *map, int mode) {
	IndexFlat *index = (IndexFlat *) idx;
	INodeFlat *node;

	if (io->dims != index->dims || io->dims_aligned != index->dims_aligned)
		return INVALID_DIMENSIONS;
    
    for (int i = 0; i < (int) io->elements; i++) {
		if (map_has(map, io->vectors[i]->id)) {
			switch (mode) {
			
			case IMPORT_OVERWITE:
				PANIC_IF(map_get_safe_p(map, io->vectors[i]->id, (void **)&node) != MAP_SUCCESS, "failed to get existing node");
                PANIC_IF(map_remove_p(map, io->vectors[i]->id) != MAP_SUCCESS, "failed to remove duplicate ID from map");
                PANIC_IF(flat_delete(index, node) != SUCCESS, "failed to delete existing node");
				node = NULL;
				break;

			case IMPORT_IGNORE_VERBOSE:
				WARNING("import", "duplicated entry - ignore");
				continue;
			case IMPORT_IGNORE:
			default:
				continue;
			}

		}
        node = calloc_mem(1, sizeof(INodeFlat));
        if (node == NULL)
            return SYSTEM_ERROR;
        node->vector = io->vectors[i];
        insert_node(&index->head, node);
		if (map_insert_p(map, node->vector->id, node) != MAP_SUCCESS)
            return SYSTEM_ERROR;
    }
    return SUCCESS;
}

/*-------------------------------------------------------------------------------------*
 *                                PUBLIC FUNCTIONS                                     *
 *-------------------------------------------------------------------------------------*/


/*
 * flat_index - Initializes a generic index structure with a flat index.
 *
 * @param idx    - Pointer to the generic Index structure.
 * @param method - Distance metric method.
 * @param dims   - Number of dimensions.
 *
 * @return SUCCESS on success, SYSTEM_ERROR on failure.
 */

 static inline void flat_functions(Index *idx) {
    idx->search   = flat_search;
    idx->search_n = flat_search_n;
    idx->insert   = flat_insert;
    idx->dump     = flat_dump;
	idx->export   = flat_export;
	idx->import   = flat_import;
	idx->compare  = flat_compare;
    idx->remap    = flat_remap;
    idx->delete   = flat_delete;
    idx->release  = flat_release;
	idx->update_icontext = NULL;
}

int flat_index(Index *idx, int method, uint16_t dims) {
    idx->data = flat_init(method, dims);
    if (idx->data == NULL) 
        return SYSTEM_ERROR;
    idx->name     = "flat";
    flat_functions(idx);

    return SUCCESS;
}

int flat_index_load(Index *idx, IOContext *io) {
    idx->data = flat_load(io);
    if (idx->data == NULL)
        return SYSTEM_ERROR;
    idx->name     = "flat";
    flat_functions(idx);

    return SUCCESS;
}
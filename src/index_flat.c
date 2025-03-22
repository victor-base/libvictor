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
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "iflat_utils.h"
#include "method.h"
#include "victor.h"
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

    pthread_rwlock_t rwlock; // Read-write lock for thread safety
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
    pthread_rwlock_init(&index->rwlock, NULL);
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
static int flat_delete(void *index, uint64_t id) {
    IndexFlat *ptr = (IndexFlat *)index;
    int ret;
    if (index == NULL) 
        return INVALID_INDEX;

    pthread_rwlock_wrlock(&ptr->rwlock);
    if ((ret = delete_node(&(ptr->head),id)) == SUCCESS) {
        ptr->elements--;
    }
    // Verificar, quiza no estaba
    pthread_rwlock_unlock(&ptr->rwlock);
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
static int flat_search_n(void *index, float32_t *vector, uint16_t dims, MatchResult **result, int n) {
    IndexFlat *idx = (IndexFlat *)index;
    INodeFlat *current;
    float32_t *v;
    int allocated = 0;

    if (index == NULL) 
        return INVALID_INDEX;
    if (vector == NULL)
        return INVALID_VECTOR;
    if (dims != idx->dims) 
        return INVALID_DIMENSIONS;
    if (result == NULL)
        return INVALID_RESULT;

    *result = (MatchResult *) calloc_mem(n, sizeof(MatchResult));
    if (*result == NULL)
        return SYSTEM_ERROR;

    if (dims < idx->dims_aligned) {
        v = (float32_t *)calloc_mem(1, idx->dims_aligned * sizeof(float32_t));
        if (v == NULL) {
            free_mem(*result);
            return SYSTEM_ERROR;
        }
        allocated = 1;
        memcpy(v, vector, dims * sizeof(float32_t));
    } else {
        v = vector;
    }

    pthread_rwlock_rdlock(&idx->rwlock);

    current = idx->head;
    if (current == NULL) {
        pthread_rwlock_unlock(&idx->rwlock);
        if (allocated)
            free_mem(v);
        free_mem(*result);
        return INDEX_EMPTY;
    }

    flat_linear_search_n(current, v, idx->dims_aligned, *result, n, idx->cmp);

    pthread_rwlock_unlock(&idx->rwlock);
    if (allocated)
        free_mem(v);
    return SUCCESS;

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
    int allocated = 0;

    if (index == NULL) 
        return INVALID_INDEX;
    if (vector == NULL)
        return INVALID_VECTOR;
    if (dims != idx->dims) 
        return INVALID_DIMENSIONS;
    if (result == NULL)
        return INVALID_RESULT;

    if (dims < idx->dims_aligned) {
        v = (float32_t *)calloc_mem(1, idx->dims_aligned * sizeof(float32_t));
        if (v == NULL)
            return SYSTEM_ERROR;
        allocated = 1;
        memcpy(v, vector, dims * sizeof(float32_t));
    } else {
        v = vector;
    }

    pthread_rwlock_rdlock(&idx->rwlock);
    
    current = idx->head;
    if (current == NULL) {
        pthread_rwlock_unlock(&idx->rwlock);
        if (allocated)
            free_mem(v);
        return INDEX_EMPTY;
    }

    flat_linear_search(current, v, idx->dims_aligned, result, idx->cmp);

    pthread_rwlock_unlock(&idx->rwlock);
    if (allocated)
        free_mem(v);
    return SUCCESS;
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
static int flat_insert(void *index, uint64_t id, float32_t *vector, uint16_t dims) {
    IndexFlat *ptr = (IndexFlat *)index;
    INodeFlat *node;
    Vector    *nvec;

    if (index == NULL)
        return INVALID_INDEX;
    if (vector == NULL)
        return INVALID_VECTOR;
    if (dims != ptr->dims) 
        return INVALID_DIMENSIONS;

    node = (INodeFlat *) malloc (sizeof(INodeFlat));
    
    if (node == NULL) 
        return SYSTEM_ERROR;

    nvec = make_vector(id, vector, dims);
    if (nvec == NULL) {
        free_mem(node);
        return SYSTEM_ERROR;
    }
    pthread_rwlock_wrlock(&ptr->rwlock);

    node->next = NULL;
    node->prev = NULL;

    node->vector = nvec;
    insert_node(&(ptr->head), node);
    ptr->elements++;

    pthread_rwlock_unlock(&ptr->rwlock);
    return SUCCESS;
}


/*
 * flat_release - Releases all resources associated with a flat index.
 *
 * @param index - Pointer to the index to be released.
 *
 * @return SUCCESS on success, INVALID_INDEX if index is NULL.
 */
static int flat_release(void **index) {
    if (!index || !*index)
        return INVALID_INDEX;

    IndexFlat *idx = *index;
    INodeFlat *ptr;

    pthread_rwlock_wrlock(&idx->rwlock);

    ptr = idx->head;
    while (ptr) {
        idx->head = ptr->next;
        idx->elements--;
        free_vector(&ptr->vector);
        free_mem(ptr);
        ptr = idx->head;
    }

    pthread_rwlock_unlock(&idx->rwlock);
    pthread_rwlock_destroy(&idx->rwlock); 
    free_mem(idx);  
    *index = NULL;
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
int flat_index(Index *idx, int method, uint16_t dims) {
    idx->data = flat_init(method, dims);
    if (idx->data == NULL) {
        free_mem(idx);
        return SYSTEM_ERROR;
    }
    idx->name     = "flat";
    idx->context  = NULL;
    idx->search   = flat_search;
    idx->search_n = flat_search_n;
    idx->insert   = flat_insert;
    idx->delete   = flat_delete;
    idx->_release = flat_release;

    return SUCCESS;
}
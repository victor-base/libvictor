/*
* index_flat_mp.c - Flat Index Implementation for Vector Cache Database
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
#include <unistd.h>
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
    INodeFlat **heads;       // Head of the doubly linked list
    long threads;            // Number of threads for parallel search
    int  rr;                 // Round-robin counter for parallel insert

    uint64_t elements;       // Number of elements stored in the index
    uint16_t dims;           // Number of dimensions for each vector
    uint16_t dims_aligned;   // Aligned dimensions for efficient memory access

    pthread_rwlock_t rwlock; // Read-write lock for thread safety
} IndexFlatMp;

typedef struct {
    float32_t *vector;
    uint16_t dims;
    MatchResult *result;
    CmpMethod *cmp;
    INodeFlat *head;
    pthread_t thread;
} ThreadData;

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

int get_num_threads()
{
    int num_threads = 1; // Valor por defecto

#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    num_threads = sysinfo.dwNumberOfProcessors;
#else
    num_threads = sysconf(_SC_NPROCESSORS_ONLN);
#endif

    return (num_threads > 0) ? num_threads : 1; // Asegurar que no sea 0
}


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


// Posible refactor para incluir soporte en win mediante una funcion externa que busque el N° de CPUs
static IndexFlatMp *flat_mp_init(int method, uint16_t dims) {
    IndexFlatMp *index = (IndexFlatMp *) calloc_mem(1,sizeof(IndexFlatMp));
    if (index == NULL)
        return NULL;

    index->cmp = get_method(method);
    if (!index->cmp) {
        free_mem(index);
        return NULL;
    }
    index->rr = 0;
    index->threads = get_num_threads();
    index->heads = calloc(index->threads, sizeof(INodeFlat *));
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
static int flat_delete_mp(void *index, uint64_t id) {
    IndexFlatMp *ptr = (IndexFlatMp *)index;
    int ret;
    int i;
    if (index == NULL) 
        return INVALID_INDEX;

    pthread_rwlock_wrlock(&ptr->rwlock);
    for (i = 0; i < ptr->threads; i++) {
        if ((ret = delete_node(&(ptr->heads[i]),id)) == SUCCESS) {
            ptr->elements--;
            break;
        }
    }
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
static int flat_search_n_mp(void *index, float32_t *vector, uint16_t dims, MatchResult **result, int n) {
    return SYSTEM_ERROR;
}



/*
 * search_mp_thread - Thread function for parallel nearest neighbor search.
 *
 * This function is executed by each thread in the multi-threaded search process.
 * It performs a linear search using `flat_linear_search` on a specific subset 
 * of the index defined in `ThreadData`.
 *
 * @param arg - Pointer to a `ThreadData` structure containing search parameters.
 */
void *search_mp_thread(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    flat_linear_search(data->head, data->vector, data->dims, data->result, data->cmp);
}

/*
 * flat_search_mp - Multi-threaded nearest neighbor search in a flat index.
 *
 * This function performs a multi-threaded search for the nearest neighbor 
 * in a linked list-based flat index, distributing the search load among 
 * multiple threads to improve performance.
 *
 * Steps:
 * 1. Validate input parameters to ensure correctness.
 * 2. Allocate memory for an aligned copy of the input vector if necessary.
 * 3. Initialize the result structure with the worst possible match value.
 * 4. Acquire a read lock to ensure consistency during the search.
 * 5. Create threads, each processing a subset of the index.
 * 6. Each thread performs a linear search on its assigned subset.
 * 7. After all threads complete, merge the best match from each thread.
 * 8. Free allocated memory and release the read lock.
 *
 * @param index  - Pointer to the multi-threaded flat index (`IndexFlatMp`).
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
static int flat_search_mp(void *index, float32_t *vector, uint16_t dims, MatchResult *result) {
    IndexFlatMp *idx = (IndexFlatMp *)index;
    ThreadData *data;
    float32_t *v;
    int allocated = 0;
    int i;

    // Validate input parameters
    if (index == NULL) 
        return INVALID_INDEX;
    if (vector == NULL)
        return INVALID_VECTOR;
    if (dims != idx->dims) 
        return INVALID_DIMENSIONS;
    if (result == NULL)
        return INVALID_RESULT;

    // Allocate aligned memory for the vector if needed
    if (dims < idx->dims_aligned) {
        v = (float32_t *)calloc_mem(1, idx->dims_aligned * sizeof(float32_t));
        if (v == NULL)
            return SYSTEM_ERROR;
        allocated = 1;
        memcpy(v, vector, dims * sizeof(float32_t));
    } else {
        v = vector;
    }

    // Allocate memory for thread data
    data = (ThreadData *)calloc_mem(idx->threads, sizeof(ThreadData));
    if (data == NULL) {
        if (allocated)
            free_mem(v);
        return SYSTEM_ERROR;
    }

    // Initialize the result with the worst possible match value
    result->distance = idx->cmp->worst_match_value;
    result->id = 0;

    // Acquire a read lock to prevent modifications while searching
    pthread_rwlock_rdlock(&idx->rwlock);
    
    // Create and launch threads for multi-threaded searching
    for (i = 0; i < idx->threads; i++) {
        data[i].vector = v;
        data[i].dims = idx->dims_aligned;
        
        // Allocate memory for individual thread results
        data[i].result = (MatchResult *)calloc_mem(1, sizeof(MatchResult));
        data[i].cmp = idx->cmp;
        data[i].head = idx->heads[i];

        pthread_create(&data[i].thread, NULL, search_mp_thread, &data[i]);
    }

    // Wait for all threads to complete and merge the best results
    for (i = 0; i < idx->threads; i++) {
        pthread_join(data[i].thread, NULL);

        // Compare results from all threads and keep the best match
        if (idx->cmp->is_better_match(data[i].result->distance, result->distance)) {
            result->id = data[i].result->id;
            result->distance = data[i].result->distance;
        }

        // Free memory allocated for individual thread results
        free_mem(data[i].result);
    }

    // Free allocated memory for thread data
    free_mem(data);

    // Release the read lock
    pthread_rwlock_unlock(&idx->rwlock);

    // Free allocated vector memory if it was created
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
static int flat_insert_mp(void *index, uint64_t id, float32_t *vector, uint16_t dims) {
    IndexFlatMp *ptr = (IndexFlatMp *)index;
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
    insert_node(&(ptr->heads[(ptr->rr++)%ptr->threads]), node);
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
static int flat_release_mp(void **index) {
    int i;
    if (!index || !*index)
        return INVALID_INDEX;

    IndexFlatMp *idx = *index;
    INodeFlat *ptr;

    pthread_rwlock_wrlock(&idx->rwlock);

    for (i = 0; i < idx->threads; i++) {
        ptr = idx->heads[i];
        while (ptr) {
            idx->heads[i] = ptr->next;
            idx->elements--;
            free_vector(&ptr->vector);
            free_mem(ptr);
            ptr = idx->heads[i];
        }
    }

    pthread_rwlock_unlock(&idx->rwlock);
    pthread_rwlock_destroy(&idx->rwlock);
    free_mem(idx->heads);
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
int flat_index_mp(Index *idx, int method, uint16_t dims) {
    idx->data = flat_mp_init(method, dims);
    if (idx->data == NULL) {
        free_mem(idx);
        return SYSTEM_ERROR;
    }
    idx->name     = "flat_mp";
    idx->context  = NULL;
    idx->search   = flat_search_mp;
    idx->search_n = flat_search_n_mp;
    idx->insert   = flat_insert_mp;
    idx->delete   = flat_delete_mp;
    idx->_release = flat_release_mp;

    return SUCCESS;
}
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
#include "index.h"
#include "heap.h"
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
    uint64_t elements;       // Number of elements stored in the index
    uint16_t dims;           // Number of dimensions for each vector
    uint16_t dims_aligned;   // Aligned dimensions for efficient memory access

    long threads;            // Number of threads for parallel search
    int  rr;                 // Round-robin counter for parallel insert

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
static int flat_delete_mp(void *index, void *ref) {
    IndexFlatMp *ptr  = (IndexFlatMp *) index;
    INodeFlat   *node = (INodeFlat *) ref;
    int ret;
    int i;
    if (index == NULL) 
        return INVALID_INDEX;

    for (i = 0; i < ptr->threads; i++) {
        if ((ret = delete_node(&(ptr->heads[i]),node)) == SUCCESS) {
            ptr->elements--;
            break;
        }
    }

    return ret;
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
    return NULL;
}

/*
 * free_thread_data - Free memory allocated for per-thread search data.
 *
 * This function releases all dynamically allocated memory within an array
 * of `ThreadData` structures, including aligned vectors and result objects.
 * It assumes that only the first `n` entries were successfully initialized.
 *
 * @param data - Pointer to the array of `ThreadData` structures.
 * @param n    - Number of valid entries to free (i.e., threads initialized).
 */
static void free_thread_data(ThreadData *data, int n) {
    for (int i = 0; i < n; i++) {
        if (data[i].vector) free_aligned_mem(data[i].vector);
        if (data[i].result) free_mem(data[i].result);
    }
    free_mem(data);
}

/*
 * make_thread_data - Allocate and initialize per-thread data for parallel search.
 *
 * This function allocates an array of `ThreadData` structures, each one holding
 * an aligned copy of the input vector and a placeholder for the match result.
 * It prepares the data needed by multiple threads to perform independent searches.
 *
 * Steps:
 * 1. Allocate memory for `idx->threads` number of `ThreadData` entries.
 * 2. For each entry:
 *    - Allocate aligned memory for a local copy of the input vector.
 *    - Allocate memory for the result structure.
 *    - Initialize comparison method and dimensions.
 *    - Copy the input vector into the allocated memory.
 * 3. If any allocation fails, free all previously allocated memory safely.
 *
 * @param data   - Output pointer to the array of `ThreadData` to be returned.
 * @param idx    - Pointer to the multi-threaded index (`IndexFlatMp`).
 * @param vector - Pointer to the input query vector.
 * @param dims   - Number of dimensions of the input vector.
 *
 * @return SUCCESS if the allocation and initialization succeed.
 *         SYSTEM_ERROR if any memory allocation fails.
 */
static int make_thread_data(ThreadData **data, const IndexFlatMp *idx, const float32_t *vector, uint16_t dims, int rsz) {
    ThreadData *thread_data = (ThreadData *)calloc_mem(idx->threads, sizeof(ThreadData));
    if (thread_data == NULL)
        return SYSTEM_ERROR;

    for (int i = 0; i < idx->threads; i++) {
        thread_data[i].vector = (float32_t *)aligned_calloc_mem(16, idx->dims_aligned * sizeof(float32_t));
        if (!thread_data[i].vector) {
            free_thread_data(thread_data, i);
            return SYSTEM_ERROR;
        }
        thread_data[i].result = (MatchResult *)calloc_mem(rsz, sizeof(MatchResult));
        if (!thread_data[i].result) {
            free_aligned_mem(thread_data[i].vector);
            free_thread_data(thread_data, i);
            return SYSTEM_ERROR;
        }

        memcpy(thread_data[i].vector, vector, dims * sizeof(float32_t));
        thread_data[i].dims = idx->dims_aligned;
        thread_data[i].cmp  = idx->cmp;
    }

    *data = thread_data;
    return SUCCESS;
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
    int ret;
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

    // Initialize the result with the worst possible match value
    result->distance = idx->cmp->worst_match_value;
    result->id = 0;


    ret = make_thread_data(&data, idx, vector, dims, 1);
    if (ret != SUCCESS) 
        return ret;


    ret = SUCCESS;
    for (i= 0; i < idx->threads; i++) {
        data[i].head = idx->heads[i];
        if (pthread_create(&data[i].thread, NULL, search_mp_thread, &data[i])!=0) {
            ret = THREAD_ERROR;
            break;
        }
            
    }
    int join_until = i;
    // Wait for all threads to complete and merge the best results
    for (i = 0; i < join_until; i++) {
        pthread_join(data[i].thread, NULL);

        // Compare results from all threads and keep the best match
        if (ret != THREAD_ERROR && idx->cmp->is_better_match(data[i].result->distance, result->distance)) {
            result->id = data[i].result->id;
            result->distance = data[i].result->distance;
        }
    }

    free_thread_data(data, idx->threads);
    return ret;
}

/*
 * flat_search_n_mp - Multi-threaded top-N nearest neighbors search in a flat index.
 *
 * This function performs a parallel search to find the top-N most similar vectors 
 * to a given query vector within a multi-threaded flat index. The search is 
 * distributed across multiple threads, each processing a subset of the data. 
 * Partial results are merged using a max-heap to determine the final top-N matches.
 *
 * Steps:
 * 1. Validate input parameters for correctness.
 * 2. Initialize the result array with the worst possible match values.
 * 3. Acquire a read lock to prevent modifications during the search.
 * 4. Allocate and initialize per-thread data structures.
 * 5. Initialize a max-heap to store candidate results across all threads.
 * 6. Create one thread per partition of the index, each performing a local search.
 * 7. Wait for all threads to finish. If all succeed, merge their results into the heap.
 * 8. Pop the top-N best matches from the heap into the final result array.
 * 9. Free all allocated resources and release the read lock.
 *
 * Notes:
 * - If any thread fails to start, the search is considered invalid and no results are returned.
 * - Result pointers from thread-local data are inserted directly into the heap.
 * - The function assumes that each thread returns `n` local results.
 *
 * @param index  - Pointer to the multi-threaded flat index (`IndexFlatMp`).
 * @param vector - Pointer to the input query vector.
 * @param dims   - Number of dimensions of the input vector.
 * @param result - Output array of `MatchResult` to store the top-N matches.
 * @param n      - Number of nearest neighbors to return.
 *
 * @return SUCCESS if the search completes successfully.
 *         INVALID_* if input validation fails.
 *         SYSTEM_ERROR if memory allocation fails.
 *         THREAD_ERROR if thread creation fails.
 */
static int flat_search_n_mp(void *index, float32_t *vector, uint16_t dims, MatchResult *result, int n) {
    IndexFlatMp *idx = (IndexFlatMp *)index;
    HeapNode node;
    Heap     heap;

    ThreadData *data;
    int ret;
    int i, k;

    // Validate input parameters
    if (index == NULL) 
        return INVALID_INDEX;
    if (vector == NULL)
        return INVALID_VECTOR;
    if (dims != idx->dims) 
        return INVALID_DIMENSIONS;
    if (result == NULL)
        return INVALID_RESULT;

    for (i = 0; i < n; i ++ ) {
        result[i].distance = idx->cmp->worst_match_value;
        result[i].id = NULL_ID;
    }

    ret = make_thread_data(&data, idx, vector, dims, n);
    if (ret != SUCCESS) 
        return ret;

    
    if (init_heap(&heap, HEAP_MAX, idx->threads * n, idx->cmp->is_better_match) != HEAP_SUCCESS) {
        free_thread_data(data, idx->threads);
        return SYSTEM_ERROR; // o un error específico como HEAP_ERROR
    }

    ret = SUCCESS;
    for (i= 0; i < idx->threads; i++) {
        data[i].head = idx->heads[i];
        if (pthread_create(&data[i].thread, NULL, search_mp_thread, &data[i])!=0) {
            ret = THREAD_ERROR;
            break;
        }
            
    }
    int join_until = i;
    // Wait for all threads to complete and merge the best results
    for (i = 0; i < join_until; i++) {
        pthread_join(data[i].thread, NULL);

        // Compare results from all threads and keep the best match
        if (ret != THREAD_ERROR) {
            for (k = 0; k < n; k ++) {
                node.distance = data[i].result[k].distance;
                HEAP_NODE_U64(node.value) = data[i].result[k].id;
                heap_insert(&heap, &node);
            }
        }
    }

    if (ret != THREAD_ERROR) {
        for ( i = 0; i < n; i++ ) {
            heap_pop(&heap, &node);
            result[i].distance = node.distance;
            result[i].id = HEAP_NODE_U64(node.value);
        }
    }
    heap_destroy(&heap);
    free_thread_data(data, idx->threads);
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
static int flat_insert_mp(void *index, uint64_t id, float32_t *vector, uint16_t dims, void **ref) {
    IndexFlatMp *ptr = (IndexFlatMp *)index;
    INodeFlat *node;

    if (index == NULL)
        return INVALID_INDEX;
    if (vector == NULL)
        return INVALID_VECTOR;
    if (dims != ptr->dims) 
        return INVALID_DIMENSIONS;

    node = (INodeFlat *) calloc_mem(1, sizeof(INodeFlat));
    
    if (node == NULL) 
        return SYSTEM_ERROR;

    node->vector = make_vector(id, vector, dims);
    if (node->vector == NULL) {
        free_mem(node);
        return SYSTEM_ERROR;
    }

    insert_node(&(ptr->heads[(ptr->rr++)%ptr->threads]), node);
    ptr->elements++;

    if (ref)
        *ref = node;

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
    idx->release  = flat_release_mp;

    return SUCCESS;
}
/*
* index.c - Index Management for Vector Cache Database
* 
* Copyright (C) 2025 Emiliano A. Billi
*
* This file implements a generic index management system that allows 
* creating, searching, inserting, and deleting vector-based index structures.
* It abstracts different indexing methods, enabling future expansion with 
* alternative indexing techniques such as HNSW, IVF, or custom tree-based indexes.
*
* Features:
* - Generic index interface supporting multiple index types.
* - Thread-safe operations using read-write locks.
* - Efficient storage and retrieval of high-dimensional vectors.
* - Modular design to support various distance metrics (L2, Cosine, etc.).
*
* How to extend:
* - Implement a new indexing structure (e.g., HNSW, KD-Tree, etc.).
* - Create a corresponding initialization function (e.g., hnsw_index).
* - Modify `alloc_index()` to include the new type.
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

#include "victor.h"
#include "mem.h"
#include "index_flat.h"
#include "index_flat_mp.h"

int search_n(Index *index, float32_t *vector, uint16_t dims, MatchResult **results, int n) {
    if (!index || !index->data || !index->search_n)
        return INVALID_INIT;
    return index->search_n(index->data, vector, dims, results, n);
}


int search(Index *index, float32_t *vector, uint16_t dims, MatchResult *result) {
    if (index && index->data && index->search)
        return (index->search(index->data, vector, dims, result));
    return INVALID_INIT;
}

int insert(Index *index, uint64_t id, float32_t *vector, uint16_t dims) {
    if (!index || !index->data || !index->insert)
        return INVALID_INIT;
    return index->insert(index->data, id, vector, dims);
}

int delete(Index *index, uint64_t id) {
    if (!index || !index->data || !index->delete)
        return INVALID_INIT;
    return index->delete(index->data, id);
}

/*
 * Destroys and deallocates an index.
 *
 * This function is responsible for safely releasing all resources associated
 * with an index, including its internal data structures and memory allocations.
 *
 * Steps:
 * 1. Checks if the index pointer is valid.
 * 2. Calls the specific release function for the index type.
 * 3. Frees the memory allocated for the index itself.
 * 4. Sets the index pointer to NULL to prevent dangling references.
 *
 * @param index - Pointer to the index structure to be destroyed.
 *
 * @return SUCCESS on successful deallocation, INVALID_INIT if the index is already NULL or uninitialized.
 */
int destroy_index(Index **index) {
    if (!index || !*index || !(*index)->data || !(*index)->_release) 
        return INVALID_INIT;
    (*index)->_release(&(*index)->data);
    free_mem(*index);
    *index = NULL;
    return SUCCESS;
}


/*
 * Allocates and initializes a new index.
 * 
 * This function is responsible for creating and configuring an index structure
 * based on the specified type. It abstracts the underlying implementation
 * and allows adding new index types in the future.
 *
 * To add a new index type:
 * 1. Define a new index type constant (e.g., HNSW_INDEX).
 * 2. Implement the initialization function for the new index (e.g., hnsw_index).
 * 3. Add a new case statement in the switch below to handle the new type.
 *
 * @param type   - Index type (e.g., FLAT_INDEX, HNSW_INDEX).
 * @param method - Distance metric method (e.g., L2NORM, COSINE).
 * @param dims   - Number of dimensions of the stored vectors.
 *
 * @return Pointer to the allocated index or NULL on failure.
 */
Index *alloc_index(int type, int method, uint16_t dims) {
    Index *idx = calloc_mem(1, sizeof(Index));
    if (idx == NULL) 
        return NULL;

    switch (type) {
        case FLAT_INDEX:
            if (flat_index(idx, method, dims) != SUCCESS) {
                free_mem(idx);
                return NULL;
            }
            break;
        
        case FLAT_INDEX_MP:
            if (flat_index_mp(idx, method, dims) != SUCCESS) {
                free_mem(idx);
                return NULL;
            }
            break;
        /*
        Future index types can be added here
        
        case HNSW_INDEX:
            if (hnsw_index(idx, method, dims) != SUCCESS) {
                free_mem(idx);
                return NULL;
            }
            break;
        */
        default:
            free_mem(idx);
            return NULL;
    }
    return idx;
}



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
 * This file is part of libvictor.
 *
 * libvictor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * libvictor is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libvictor. If not, see <https://www.gnu.org/licenses/>.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <https://www.gnu.org/licenses/>.
*
* Contact: emiliano.billi@gmail.com
*/

#include "config.h"
#define __LIB_CODE 1

#include "index.h"
#include "mem.h"
#include "map.h"
#include "time.h"
#include "index_flat.h"
#include "index_flat_mp.h"


/**
 * Returns the version of the library.
 */
const char *__LIB_VERSION() {
    static const char version[] = "libvictor " __LIB_VERSION_MAJOR "." __LIB_VERSION_MINOR "." __LIB_VERSION_PATCH
                                                " (" ARCH " - " OS ")"
                                                " [" __DATE__ " " __TIME__ "]";
    return version;
}


#define UPDATE_TIMESTAT(stat, delta)                   \
    do {                                               \
        (stat).count++;                                \
        (stat).total += (delta);                       \
		(stat).last = (delta);						   \
        if ((stat).count == 1) {                       \
            (stat).min = (stat).max = (delta);         \
        } else {                                       \
            if ((delta) < (stat).min) (stat).min = delta; \
            if ((delta) > (stat).max) (stat).max = delta; \
        }                                              \
    } while(0)



int search_n(Index *index, float32_t *vector, uint16_t dims, MatchResult *results, int n) {
	double start, end, delta;
	int ret;

	if (index == NULL)  return INVALID_INDEX;
	if (vector == NULL) return INVALID_VECTOR;
	if (results == NULL) return INVALID_RESULT;

	if (index->data == NULL || index->search_n == NULL)
		return INVALID_INIT;
    
	pthread_rwlock_rdlock(&index->rwlock);
	start = get_time_ms_monotonic();
	ret = index->search_n(index->data, vector, dims, results, n);
	end = get_time_ms_monotonic();

	if (ret == SUCCESS) {
		delta = end - start;
		UPDATE_TIMESTAT(index->stats.search_n, delta);
	}
	pthread_rwlock_unlock(&index->rwlock);
	return ret;
}


int search(Index *index, float32_t *vector, uint16_t dims, MatchResult *result) {
	double start, end, delta;
	int ret;

	if (index == NULL)  return INVALID_INDEX;
	if (vector == NULL) return INVALID_VECTOR;
	if (result == NULL) return INVALID_RESULT;

	if (index->data == NULL || index->search == NULL)
		return INVALID_INIT;

	pthread_rwlock_rdlock(&index->rwlock);
	start = get_time_ms_monotonic();
    ret = index->search(index->data, vector, dims, result);
    end = get_time_ms_monotonic();

	if (ret == SUCCESS) {
		delta = end - start;
		UPDATE_TIMESTAT(index->stats.search, delta);
	}
	pthread_rwlock_unlock(&index->rwlock);
	return ret;
}

int insert(Index *index, uint64_t id, float32_t *vector, uint16_t dims) {
	double start, end, delta;
	void *ref;
	int ret;

	if (id == NULL_ID)  return INVALID_ID;
	if (index == NULL)  return INVALID_INDEX;
	if (vector == NULL) return INVALID_VECTOR;

	if (index->data == NULL || index->insert == NULL)
		return INVALID_INIT;
	
	pthread_rwlock_wrlock(&index->rwlock);

	if (map_has(&index->map, id) == 1) {
		ret = DUPLICATED_ENTRY;
		goto cleanup;
	}

	start = get_time_ms_monotonic();
	ret = index->insert(index->data, id, vector, dims, &ref);
	end = get_time_ms_monotonic();
	if (ret == SUCCESS) {
		if ((ret = map_insert(&index->map, id, ref)) != MAP_SUCCESS) {
			PANIC_IF(index->delete(index->data, ref) != SUCCESS, "lack of consistency on delete after insert");
			goto cleanup;
		}
		delta = end - start;
		UPDATE_TIMESTAT(index->stats.insert, delta);
	}

cleanup:
	pthread_rwlock_unlock(&index->rwlock);
	return ret;
}

int delete(Index *index, uint64_t id) {
	void *ref;
	double start, end, delta;
	int ret;

	if (id == NULL_ID)  return INVALID_ID;
	if (index == NULL)  return INVALID_INDEX;
	if (!index->data || !index->delete)
		return INVALID_INIT;
	
	pthread_rwlock_wrlock(&index->rwlock);
	start = get_time_ms_monotonic();
    
	ref = map_ref(&index->map, id);
	if (ref == NULL) {
		ret = NOT_FOUND_ID;
		goto cleanup;
	}

	ret = index->delete(index->data, ref);
	PANIC_IF(ret != SUCCESS, "lack of consistency using index->delete");
	PANIC_IF(map_remove(&index->map, id) == NULL, "lack of consistency using map_remove");

	end = get_time_ms_monotonic();
	delta = end - start;
	UPDATE_TIMESTAT(index->stats.delete, delta);

cleanup:
	pthread_rwlock_unlock(&index->rwlock);
	return ret;
}

int stats(Index *index, IndexStats *stats) {
	if (!index)
		return INVALID_INDEX;
	if (!stats)
		return INVALID_ARGUMENT;
	pthread_rwlock_rdlock(&index->rwlock);
	*stats = index->stats;
	pthread_rwlock_unlock(&index->rwlock);
	return SUCCESS;
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
    if (!index || !*index)
		return INVALID_INDEX;
	if (!(*index)->data || !(*index)->release) 
        return INVALID_INIT;

	pthread_rwlock_wrlock(&(*index)->rwlock);
    (*index)->release(&(*index)->data);
	map_destroy(&(*index)->map);
	pthread_rwlock_unlock(&(*index)->rwlock);
    pthread_rwlock_destroy(&(*index)->rwlock); 
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
Index *alloc_index(int type, int method, uint16_t dims, void *icontext) {
    Index *idx = calloc_mem(1, sizeof(Index));
    int ret;
	if (idx == NULL || icontext != NULL) 
        return NULL;

	ret = init_map(&idx->map, 10000, 15);
	if (ret != SUCCESS) {
		free_mem(idx);
		return NULL;
	}

	pthread_rwlock_init(&idx->rwlock, NULL);
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



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
#include "heap.h"
#include "mem.h"
#include "map.h"
#include "vtime.h"
#include "kmeans.h"
#include "method.h"
#include "index_flat.h"
#include "index_hnsw.h"


/**
 * Returns the version of the library.
 */
const char *__LIB_VERSION() {
    static const char version[] = "libvictor " __LIB_VERSION_MAJOR "." __LIB_VERSION_MINOR "." __LIB_VERSION_PATCH
                                                " (" ARCH " - " OS ")"
                                                " [" __DATE__ " " __TIME__ "]";
    return version;
}

const char *__LIB_SHORT_VERSION() {
    static const char version[] = __LIB_VERSION_MAJOR "." __LIB_VERSION_MINOR "." __LIB_VERSION_PATCH;
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



/*
 * Searches for the `n` nearest vectors in the index to a given query vector.
 *
 * This function performs a k-nearest neighbor (k-NN) search using the specified
 * query vector and retrieves up to `n` results, sorted by increasing distance
 * or decreasing similarity, depending on the index’s distance method.
 *
 * Steps:
 * 1. Validates input parameters (`index`, `vector`, and `results` must be non-NULL).
 * 2. Ensures the index is properly initialized and provides a `search_n` implementation.
 * 3. Acquires a read lock to safely access shared data without blocking other readers.
 * 4. Records the start time for statistics collection.
 * 5. Calls the backend-specific `search_n` function to obtain the top `n` matches.
 * 6. If the search succeeds, records the elapsed time in the index's statistics.
 * 7. Releases the read lock and returns the result status.
 *
 * @param index   - Pointer to the index structure to be searched.
 * @param vector  - Pointer to the query vector.
 * @param dims    - Number of dimensions of the query vector.
 * @param results - Pointer to an array of `MatchResult` structures to hold results.
 * @param n       - Maximum number of nearest neighbors to retrieve.
 *
 * @return SUCCESS if search completed successfully,
 *         or an appropriate error code (e.g., INVALID_INDEX, INVALID_RESULT, or backend-specific error).
 */

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

/*
 * Searches for the nearest vector in the index to a given query vector.
 *
 * This function performs a single nearest neighbor search using the specified
 * query vector. It retrieves the most similar vector stored in the index
 * based on the index’s configured distance metric (e.g., L2, cosine).
 *
 * Steps:
 * 1. Validates the input parameters (`index`, `vector`, and `result` must be non-NULL).
 * 2. Ensures the index is properly initialized with a search function and data backend.
 * 3. Acquires a read lock to allow concurrent queries while ensuring consistency.
 * 4. Records the start time for performance statistics.
 * 5. Invokes the backend-specific `search` function to find the nearest neighbor.
 * 6. If the search is successful, records the elapsed time into the index’s statistics.
 * 7. Releases the read lock and returns the status.
 *
 * @param index  - Pointer to the index structure to be searched.
 * @param vector - Pointer to the query vector.
 * @param dims   - Number of dimensions of the query vector.
 * @param result - Pointer to a `MatchResult` structure that will hold the best match.
 *
 * @return SUCCESS if a match was found,
 *         or an appropriate error code (e.g., INVALID_INDEX, INVALID_RESULT, or backend-specific error).
 */

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

/**
 * @brief Filters and ranks a subset of elements from an index based on similarity
 *        to a query vector, returning the top-N closest matches.
 *
 * This function compares a given input vector against a subset of indexed elements
 * identified by their IDs, using the configured comparison method in the index.
 * It maintains a heap to efficiently track the top-N best matches according to the
 * selected similarity or distance metric.
 *
 * Results are written into the provided `results` array, sorted from best to worst match.
 * If fewer than N valid elements are found, the remaining entries are filled with
 * default values (`id = 0`, and `distance = cmp->worst_match_value`).
 *
 * Thread-safe read access to the index is ensured via a shared read-lock.
 *
 * @param index    Pointer to the index containing elements and configuration.
 * @param ids      Array of element IDs to compare against.
 * @param i        Number of IDs in the `ids` array.
 * @param vector   Pointer to the input vector to be compared.
 * @param dims     Dimensionality of the input vector.
 * @param results  Output array of `MatchResult` structures to store top-N matches.
 * @param n        Maximum number of top matches to return.
 *
 * @return SUCCESS on success, or an appropriate error code on failure.
 */
int filter_subset(Index *index, uint64_t *ids, int i, float32_t *vector, uint16_t dims, MatchResult *results, int n) {
	HeapNode e;
	CmpMethod *cmp;
	void *node;
	Heap W;
	int ret = SUCCESS;

	if (index == NULL)   return INVALID_INDEX;
    if (vector == NULL)  return INVALID_VECTOR;
    if (results == NULL) return INVALID_RESULT;

    if (index->data == NULL || index->compare == NULL)
        return INVALID_INIT;

	cmp = get_method(index->method);
	if (!cmp)
		return INVALID_INIT;

	if (init_heap(&W, HEAP_WORST_TOP, n, cmp->is_better_match) != HEAP_SUCCESS)
		return SYSTEM_ERROR;

	pthread_rwlock_rdlock(&index->rwlock);
	for (int j = 0; j < i; j++) {
		float32_t distance;
		
		if (map_get_safe_p(&index->map, ids[j], &node) != MAP_SUCCESS) 
			continue;

		if ((ret = index->compare(index->data, node , vector, dims, &distance)) != SUCCESS) {
			pthread_rwlock_unlock(&index->rwlock);
			goto end;
		}
		e = HEAP_NODE_SET_U64(ids[j], distance);
		PANIC_IF(heap_insert_or_replace_if_better(&W, &e) != HEAP_SUCCESS, "error in heap");
	}
	pthread_rwlock_unlock(&index->rwlock);
	int heap_len = heap_size(&W);
	int pos = 0;

	while (pos < heap_len) {
		PANIC_IF(heap_pop(&W, &e)!= HEAP_SUCCESS, "lack");
		
		results[heap_len - pos - 1].id = HEAP_NODE_U64(e);
		results[heap_len - pos - 1].distance = e.distance;
		pos++;
	}

	for (int j = heap_len; j < n; j++) {
		results[j].id = 0;
		results[j].distance = cmp->worst_match_value;
	}
end:
	heap_destroy(&W);
	return ret;
}

/*
 * Inserts a new vector into the index with a specified ID.
 *
 * This function inserts a vector into the underlying index structure and
 * registers its associated ID in the internal map for fast lookup and deletion.
 * It handles duplication checks and rollback logic in case the secondary insert
 * into the ID map fails, ensuring consistency between data structures.
 *
 * Steps:
 * 1. Validates the input parameters (ID, vector pointer, index structure).
 * 2. Ensures the index is properly initialized and contains the required function pointers.
 * 3. Acquires a write lock to ensure exclusive access during modification.
 * 4. Checks whether the given ID already exists to prevent duplicates.
 * 5. Records the start time for statistical purposes.
 * 6. Calls the backend-specific `insert` function to add the vector to the structure.
 * 7. If the insert succeeds, attempts to register the ID and its reference in the internal map.
 *    - If map insertion fails, deletes the previously inserted vector to maintain consistency.
 * 8. Updates timing statistics for the insert operation.
 * 9. Releases the write lock and returns the result.
 *
 * @param index  - Pointer to the target index structure.
 * @param id     - Unique identifier for the vector.
 * @param vector - Pointer to the vector data to be inserted.
 * @param dims   - Dimensionality of the vector.
 *
 * @return SUCCESS on successful insertion,
 *         DUPLICATED_ENTRY if the ID already exists,
 *         or appropriate error code on failure (e.g., INVALID_VECTOR, MAP_ERROR).
 */

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
        if ((ret = map_insert_p(&index->map, id, ref)) != MAP_SUCCESS) {
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

int update_icontext(Index *index, void *context, int mode) {
    int ret;
	if (index->data == NULL)
        return INVALID_INIT;

	if (index->update_icontext == NULL)
		return NOT_IMPLEMENTED;
    
    pthread_rwlock_wrlock(&index->rwlock);
	ret = index->update_icontext(index->data, context, mode);
    pthread_rwlock_unlock(&index->rwlock);
	return ret;
}

/*
 * Deletes a vector from the index by its ID.
 *
 * This function removes a vector associated with the given ID from both
 * the index’s internal data structures and its ID map. It ensures consistency
 * by validating the state of the index and checking for the presence of
 * the vector before deletion.
 *
 * Steps:
 * 1. Validates the input parameters (non-null ID, valid index, proper function pointers).
 * 2. Acquires a write lock to ensure exclusive access during the mutation.
 * 3. Looks up the internal reference to the vector using the ID.
 * 4. If the ID is not found, returns NOT_FOUND_ID.
 * 5. Calls the backend-specific `delete` function to remove the vector from the underlying structure.
 * 6. Removes the ID from the internal map to maintain consistency.
 * 7. Measures and records the operation’s duration in the deletion statistics.
 * 8. Releases the write lock before returning.
 *
 * @param index - Pointer to the index from which to delete a vector.
 * @param id    - ID of the vector to be deleted.
 *
 * @return SUCCESS if deleted successfully,
 *         NOT_FOUND_ID if the ID does not exist,
 *         or appropriate error code on failure (e.g., INVALID_INDEX, INVALID_INIT).
 */

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
    
    ref = map_get_p(&index->map, id);
    if (ref == NULL) {
        ret = NOT_FOUND_ID;
        goto cleanup;
    }

    ret = index->delete(index->data, ref);
    PANIC_IF(ret != SUCCESS, "lack of consistency using index->delete");
    PANIC_IF(map_remove_p(&index->map, id) == NULL, "lack of consistency using map_remove");

    end = get_time_ms_monotonic();
    delta = end - start;
    UPDATE_TIMESTAT(index->stats.delete, delta);

cleanup:
    pthread_rwlock_unlock(&index->rwlock);
    return ret;
}
int cpp_delete(Index *index, uint64_t id) {
    return delete(index, id);
}
/*
 * Retrieves internal timing statistics of an index.
 *
 * This function provides aggregate performance metrics related to operations
 * on the index, such as insertions, deletions, and searches. The values include
 * total time spent, minimum and maximum execution times, and operation count.
 *
 * Steps:
 * 1. Validates the input pointers.
 * 2. Acquires a read lock to ensure consistent access to statistics.
 * 3. Copies the internal statistics into the provided `IndexStats` structure.
 * 4. Releases the lock.
 *
 * @param index - Pointer to the index from which to retrieve statistics.
 * @param stats - Pointer to a user-allocated `IndexStats` struct where results will be stored.
 *
 * @return SUCCESS on success, INVALID_INDEX if the index is NULL, or INVALID_ARGUMENT if `stats` is NULL.
 */

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
 * Returns the number of elements currently stored in the index.
 *
 * This function provides the total number of unique vectors inserted
 * and currently active in the index. It does not include deleted or
 * invalidated entries.
 *
 * Steps:
 * 1. Validates the index pointer.
 * 2. Acquires a read lock to safely access the internal map structure.
 * 3. Retrieves the current element count from the map and stores it in `*sz`.
 * 4. Releases the lock.
 *
 * @param index - Pointer to the index whose size will be retrieved.
 * @param sz    - Pointer to a uint64_t variable where the size will be stored.
 *
 * @return SUCCESS on success, INVALID_INDEX if the index is NULL.
 */
int size(Index *index, uint64_t *sz) {
    if (!index)
        return INVALID_INDEX;
    pthread_rwlock_rdlock(&index->rwlock);
    *sz = index->map.elements;
    pthread_rwlock_unlock(&index->rwlock);
    return SUCCESS;
}

/*
 * Checks whether a vector with the given ID exists in the index.
 *
 * This function determines if a vector with the specified ID is
 * currently present in the index. It uses the internal ID map to
 * perform the lookup efficiently.
 *
 * Steps:
 * 1. Validates the index pointer.
 * 2. Acquires a read lock to ensure thread-safe access.
 * 3. Uses the internal map to check for the presence of the ID.
 * 4. Releases the lock and returns the result.
 *
 * @param index - Pointer to the index.
 * @param id    - The ID of the vector to check for.
 *
 * @return 1 if the ID exists, 0 otherwise. Returns 0 if the index is NULL.
 */

int contains(Index *index, uint64_t id) {
    int ret;
    if (!index)
        return 0;
    pthread_rwlock_rdlock(&index->rwlock);
    ret = map_has(&index->map, id);
    pthread_rwlock_unlock(&index->rwlock);
    return ret;
}

/*
 * Dumps the current index state to a file on disk.
 *
 * This function serializes the internal structure and data of the index,
 * including vectors, metadata, and any algorithm-specific state (e.g., graph links).
 * The resulting file can later be used to restore the index via a corresponding load operation.
 *
 * @param index - Pointer to the index instance.
 * @param filename - Path to the output file where the index will be saved.
 *
 * @return SUCCESS on success,
 *         INVALID_INDEX if the index is NULL,
 *         NOT_IMPLEMENTED if the index type does not support dumping,
 *         or SYSTEM_ERROR on I/O failure.
 */
int dump(Index *index, const char *filename) {
    double start, end, delta;
    IOContext io;
    
    int ret;
    if (!index)
        return INVALID_INDEX;
    
    if (index->dump == NULL)
        return NOT_IMPLEMENTED;

    pthread_rwlock_rdlock(&index->rwlock);
    start = get_time_ms_monotonic();
    ret = index->dump(index->data, &io);
    if (ret == SUCCESS) {
        ret = store_dump_file(filename, &io);
        if (ret == SUCCESS) {
            end = get_time_ms_monotonic();
            delta = end - start;
            UPDATE_TIMESTAT(index->stats.dump, delta);
        }
    }
    pthread_rwlock_unlock(&index->rwlock);
    io_free(&io);
    return ret;
}

/*
 * Export the current index state to a file on disk.
 *
 * This function serializes vectors.
 * The resulting file can later be used to import vector in the index via a corresponding import operation.
 *
 * @param index - Pointer to the index instance.
 * @param filename - Path to the output file where the index will be saved.
 *
 * @return SUCCESS on success,
 *         INVALID_INDEX if the index is NULL,
 *         NOT_IMPLEMENTED if the index type does not support dumping,
 *         or SYSTEM_ERROR on I/O failure.
 */
int export(Index *index, const char *filename) {
    IOContext io;
    
    int ret;
    if (!index)
        return INVALID_INDEX;
    
    if (index->export == NULL)
        return NOT_IMPLEMENTED;

    pthread_rwlock_rdlock(&index->rwlock);
    ret = index->export(index->data, &io);
    if (ret == SUCCESS)
        ret = store_dump_file(filename, &io);
    pthread_rwlock_unlock(&index->rwlock);
    io_free(&io);
    return ret;
}

/*
 * Import vectors from a file and populate the index.
 *
 * This function reads a previously exported file (using `export`) and loads its
 * contents into the current index instance, respecting the specified import mode.
 *
 * @param index - Pointer to the index instance where the vectors will be imported.
 * @param filename - Path to the file containing the serialized vectors to import.
 * @param mode - Import mode that determines how to handle duplicate IDs
 *               (e.g., overwrite, ignore, etc.).
 *
 * @return SUCCESS on success,
 *         INVALID_INDEX if the index is NULL,
 *         NOT_IMPLEMENTED if the index type does not support import,
 *         SYSTEM_ERROR on I/O failure or allocation error.
 */
int import(Index *index, const char *filename, int mode) {
	IOContext io;
	int ret;

	if (!index)
		return INVALID_INDEX;
	if (index->import == NULL)
		return NOT_IMPLEMENTED;

	if ((ret = store_load_file(filename, &io)) != SUCCESS)
		return ret;
	
	pthread_rwlock_wrlock(&index->rwlock);
	ret = index->import(index->data, &io, &index->map, mode);
	pthread_rwlock_unlock(&index->rwlock);
	io_free(&io);
	return ret;
}

/**
 * @brief Generate a set of centroids for K-Means clustering from an existing index.
 *
 * This function takes an existing index (`from`) and performs a clustering algorithm (like K-Means++)
 * to compute `nprobe` centroids, returning them as a new index structure containing only the centroids.
 *
 * @param from Pointer to the source Index structure containing the dataset.
 * @param nprobe The number of centroids (clusters) to generate.
 *
 * @note The returned index is a flat structure containing only the centroid vectors.
 * @note The IDs of the centroid vectors in the resulting index start from 1 up to `nprobe`.
 * @note The caller is responsible for freeing the returned index with the appropriate destruction function (e.g., destroy_index()).
 * @note If the input index is empty or if `nprobe` exceeds the number of available vectors, the function may return NULL.
 *
 * @return A pointer to the new Index containing the `nprobe` centroids, or NULL on failure.
 */
Index *kmeans_centroids(Index *from, int nprobe) {
	Index *index = NULL;
	KMContext *context  = NULL;
	float32_t **dataset = NULL;
	int dims_aligned;
	IOContext io;
	int ret;

	if (!from || !from->data)
		return NULL;
	if (from->export == NULL || from->insert == NULL)
		return NULL;

	pthread_rwlock_rdlock(&from->rwlock);
    ret = from->export(from->data, &io);
	if (ret != SUCCESS) {
		pthread_rwlock_unlock(&from->rwlock);
		return NULL;
	}

	if ((int)io.elements <= nprobe) 
		goto unlock_error_return;

	dataset = raw_vectors(io.vectors, io.elements);
	if (!dataset) 
		goto unlock_error_return;


	context = kmeans_create_context(nprobe, dataset, io.elements, io.dims_aligned, 0.001, 100);
	if (!context)
		goto unlock_error_return;

	if (kmeans_pp_train(context) != SUCCESS) 
		goto unlock_error_return;

	dims_aligned = io.dims_aligned;
	io_free(&io);
	free_mem(dataset);
	pthread_rwlock_unlock(&from->rwlock);
	

	index = alloc_index(FLAT_INDEX, L2NORM, dims_aligned, NULL);
	if (index == NULL) {
		kmeans_destroy_context(&context);
		return NULL;
	}

	for (int i = 0; i < context->c; i++) 
		if (insert(index, i+1, context->centroids[i], dims_aligned) != SUCCESS) {
			destroy_index(&index);
			break;
		}
	
	kmeans_destroy_context(&context);
	return index;

unlock_error_return:
	if (dataset) free_mem(dataset);
	if (context) kmeans_destroy_context(&context);
	io_free(&io);
	pthread_rwlock_unlock(&from->rwlock);
	return NULL;
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

const char *index_name(Index *index) {
	if (!index || !index->data)
		return "{}";

	return index->name;
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
    if (idx == NULL) 
        return NULL;

    idx->map = MAP_INIT();

    switch (type){
    case FLAT_INDEX:
        ret = flat_index(idx, method, dims);
        break;

	case HNSW_INDEX:
		ret = hnsw_index(idx, method, dims, icontext);
		break;
    default:
        ret = INVALID_INDEX;
        break;
    }

    if (ret != SUCCESS || (init_map(&idx->map, 100000, 15) != SUCCESS))
        goto error_return;

    pthread_rwlock_init(&idx->rwlock, NULL);
	idx->method = method;
    return idx;

error_return:
    if (idx && idx->data != NULL)
        idx->release(&(idx->data));
    map_destroy(&idx->map);
    free_mem(idx);
    return NULL;
}

int safe_alloc_index(Index **index, int type, int method, uint16_t dims, void *icontext) {
	if (dims == 0)
		return INVALID_DIMENSIONS;
	if (get_method(method) == NULL) 
		return INVALID_METHOD;
	if (type == FLAT_INDEX || type == HNSW_INDEX ) {
		*index = alloc_index(type, method, dims, icontext);
		if (!*index)
			return SYSTEM_ERROR;
	}
	return SUCCESS;
}

Index *load_index(const char *filename) {
    Index *idx = NULL; 
    IOContext io;
    int ret;

    if ((idx = calloc_mem(1, sizeof(Index))) == NULL)
        return NULL;

    idx->map = MAP_INIT();

    ret = store_load_file(filename, &io);
    if (ret != SUCCESS) { 
        free_mem(idx);
        return NULL;
    }

    switch (io.itype) {
        case FLAT_INDEX:
            ret = flat_index_load(idx, &io);
            break;
        default:
            ret = NOT_IMPLEMENTED;
            break;
    }
    if (ret != SUCCESS)
        goto error_return;
    
    if (init_map(&idx->map, io.elements/10, 15) != SUCCESS) {
        idx->release(&(idx->data));
        goto error_return;
    }

    if (idx->remap(idx->data, &idx->map) != SUCCESS) {
        idx->release(&(idx->data));
        goto error_return;
    }

    pthread_rwlock_init(&idx->rwlock, NULL);
	idx->method = io.method;
	io_free(&io);
    return idx;

error_return:
    map_destroy(&idx->map);
    free_mem(idx);
    io_free_vectors(&io);
    io_free(&io);
    return NULL;
}
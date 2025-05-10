/*
* victo.h - Index Structure and Management for Vector Database
* 
* Copyright (C) 2025 Emiliano A. Billi
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
*
* Contact: emiliano.billi@gmail.com
*
* Purpose:
* This header defines the `Index` structure, which serves as an abstraction
* for various types of vector indices (e.g., Flat, HNSW, IVF). It provides
* function pointers for searching, inserting, deleting, and managing indices.
*/

#ifndef __VICTOR_H
#define __VICTOR_H 1

#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

typedef float float32_t;

#define NULL_ID 0

typedef struct {
    uint64_t  id;                  // ID of the matched vector
    float32_t distance;      // Distance or similarity score
} MatchResult;

/**
 * Enumeration of available comparison methods.
 */
#define L2NORM 0x00  // Euclidean Distance
#define COSINE 0x01  // Cosine Similarity
#define DOTP   0x02  // Dot Product

#include <stdio.h>

#define PRINT_VECTOR(where,vec, dims)                              \
    do {                                                     \
        printf("%s [", (where));                                         \
        for (int _i = 0; _i < (dims); ++_i) {                \
            printf("%s%.4f", (_i == 0 ? "" : ", "), (vec)[_i]); \
        }                                                    \
        printf("]\n");                                       \
    } while (0)


/**
 * Enumeration of error codes returned by index operations.
 */
typedef enum {
    SUCCESS,
    INVALID_INIT,
    INVALID_INDEX,
    INVALID_VECTOR,
    INVALID_RESULT,
    INVALID_DIMENSIONS,
    INVALID_ARGUMENT,
    INVALID_ID,
    INVALID_REF,
    INVALID_METHOD,
    DUPLICATED_ENTRY,
    NOT_FOUND_ID,
    INDEX_EMPTY,
    THREAD_ERROR,
    SYSTEM_ERROR,
    FILEIO_ERROR,
    NOT_IMPLEMENTED,
    INVALID_FILE,
} ErrorCode;

/**
 * victor_strerror - Returns a human-readable error message for an ErrorCode.
 *
 * This function maps each error code to a descriptive string suitable
 * for logs, stderr, or user-facing error messages.
 *
 * @param code  The ErrorCode value.
 *
 * @return A constant string with the error description.
 */
extern const char *victor_strerror(ErrorCode code);

/**
 * Constants for index types.
 */
#define FLAT_INDEX    0x00  // Sequential flat index (single-threaded)
#define NSW_INDEX     0x02  // Navigable Small World graph
#define HNSW_INDEX    0x03  // Hierarchical NSW (planned)

/**
 * Statistics structure for timing measurements.
 */
typedef struct {
    uint64_t count;      // Number of operations
    double   total;      // Total time in seconds
    double   last;		 // Last operation time
    double   min;        // Minimum operation time
    double   max;        // Maximum operation time
} TimeStat;

/**
 * Aggregate statistics for the index.
 */
typedef struct {
    TimeStat insert;     // Insert operations timing
#ifdef __cplusplus
    TimeStat remove;
#else
    TimeStat delete;     // Delete operations timing
#endif
    TimeStat dump;       // Dump to file operation
    TimeStat search;     // Single search timing
    TimeStat search_n;   // Multi-search timing
} IndexStats;

/*
 * NSW Specific Struct
 */
#define OD_PROGESIVE  0x00
#define EF_AUTOTUNED  0x00
typedef struct {
    int ef_search;
    int ef_construct;
    int odegree;
} NSWContext;

#define HNSW_CONTEXT 0x01
#define HNSW_CONTEXT_SET_EF_CONSTRUCT 1 << 2
#define HNSW_CONTEXT_SET_EF_SEARCH    1 << 3
#define HNSW_CONTEXT_SET_M0           1 << 4
typedef struct {
    int ef_search;
    int ef_construct;
    int M0;
} HNSWContext;

#ifndef _LIB_CODE

typedef struct Index Index;

/**
 * Returns the version string of the library.
 */
extern const char *__LIB_VERSION();

/**
 * Returns the short version string of the library x.y.z.
 */
extern const char *__LIB_SHORT_VERSION();

/**
 * Searches for the `n` nearest neighbors using the provided index.
 * Wrapper for Index->search_n.
 */
extern int search_n(Index *index, float32_t *vector, uint16_t dims, MatchResult *results, int n);

/**
 * Searches for the closest match using the provided index.
 * Wrapper for Index->search.
 */
extern int search(Index *index, float32_t *vector, uint16_t dims, MatchResult *result);

/**
 * Inserts a vector with its ID into the index.
 * Wrapper for Index->insert.
 */
extern int insert(Index *index, uint64_t id, float32_t *vector, uint16_t dims);

/**
 * Deletes a vector from the index by ID.
 * Wrapper for Index->delete.
 */
#ifdef __cplusplus
extern int cpp_delete(Index *index, uint64_t id); 
#else
extern int delete(Index *index, uint64_t id);  
#endif


/**
 * Update Index Context 
 */
extern int update_icontext(Index *index, void *icontext, int mode);

/**
 * Retrieves the internal statistics of the index.
 *
 * This function copies the internal timing and operation statistics
 * (insert, delete, search, search_n) into the provided `IndexStats` structure.
 *
 * @param index - Pointer to the index instance.
 * @param stats - Pointer to the structure where statistics will be stored.
 *
 * @return SUCCESS on success, INVALID_INDEX or INVALID_ARGUMENT on error.
 */
extern int stats(Index *index, IndexStats *stats);

/**
 * Retrieves the current number of elements in the index.
 *
 * This function returns the number of vector entries currently stored
 * in the index, regardless of their internal structure or state.
 *
 * @param index - Pointer to the index instance.
 * @param sz - Pointer to a uint64_t that will receive the size.
 *
 * @return SUCCESS on success, INVALID_INDEX on error.
 */
extern int size(Index *index, uint64_t *sz);

/**
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
extern int dump(Index *index, const char *filename);


/**
 * Checks whether a given vector ID exists in the index.
 *
 * This function verifies the presence of a vector with the specified ID
 * within the index's internal map structure.
 *
 * @param index - Pointer to the index instance.
 * @param id - The unique vector ID to check.
 *
 * @return 1 if the ID is found, 0 if not, or 0 if the index is NULL.
 */
extern int contains(Index *index, uint64_t id);
/**
 * Allocates and initializes a new index of the specified type.
 * @param type Index type (e.g., FLAT_INDEX).
 * @param method Distance method (e.g., L2NORM or COSINE).
 * @param dims Number of dimensions of vectors.
 * @param icontext Optional context or configuration for index setup.
 * @return A pointer to the newly allocated index, or NULL on failure.
 */
extern Index *alloc_index(int type, int method, uint16_t dims, void *icontext);

/**
 * Return index name
 */
extern const char* index_name(Index *index);
/**
 * Loads an index from a previously dumped file.
 *
 * This function deserializes the contents of a file generated by `dump()`
 * and reconstructs the corresponding index structure in memory.
 *
 * @param filename - Path to the file containing the dumped index data.
 * @return A pointer to the restored index, or NULL on failure.
 */
extern Index *load_index(const char *filename);

/**
 * Releases all resources associated with the index.
 * @param index Double pointer to the index to be destroyed.
 * @return 0 if successful, or -1 on error.
 */
extern int destroy_index(Index **index);
#endif

/*
 * Asynchronous Top-K Sort (ASort) implementation using a best-heap.
 */
typedef struct ASort ASort;

/**
 * @brief Initializes an ASort context.
 *
 * Allocates and initializes the internal heap used to store top-k matches.
 *
 * @param[in,out] as Pointer to the ASort context to initialize.
 * @param[in] n Maximum number of elements to maintain in the heap.
 * @param[in] method Matching method identifier for comparison.
 * @return SUCCESS on success, or an error code on failure.
 */
extern int init_asort(ASort *as, int n, int method);

/**
 * @brief Adds multiple match results into the ASort structure.
 *
 * Inserts match results into the heap, keeping only the best k elements.
 * If the heap is full, it replaces the worst element if a better match is found.
 *
 * @param[in,out] as Pointer to the ASort context.
 * @param[in] inputs Array of match results to insert.
 * @param[in] n Number of match results in the input array.
 * @return SUCCESS on success, or an error code on failure.
 */
extern int as_update(ASort *as, MatchResult *inputs, int n);

/**
 * @brief Finalizes the ASort context and extracts sorted results.
 *
 * Pops elements from the internal heap into the output array in approximate order.
 * If the output array is NULL, simply releases internal resources.
 *
 * @param[in,out] as Pointer to the ASort context.
 * @param[out] outputs Array to store the extracted match results, or NULL to just free resources.
 * @param[in] n Maximum number of results to extract.
 * @return Number of results extracted on success, 0 if only freed, or an error code on failure.
 */
extern int as_close(ASort *as, MatchResult *outputs, int n);


#endif //* __VICTOR_H */


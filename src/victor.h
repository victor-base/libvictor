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
 * Constants for index types.
 */
#define FLAT_INDEX    0x00  // Sequential flat index (single-threaded)
#define FLAT_INDEX_MP 0x01  // Flat index with multi-threaded support
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
    TimeStat delete;     // Delete operations timing
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
extern int delete(Index *index, uint64_t id);

/**
 * Update Index Context 
 */
extern int update_icontext(Index *index, void *icontext);

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

#endif //* __VICTOR_H */


/*
* index.h - Index Structure and Management for Vector Database
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

typedef struct {
    int id;                  // ID of the matched vector
    float32_t distance;      // Distance or similarity score
} MatchResult;

/**
 * Enumeration of available comparison methods.
 */
#define L2NORM 0x00  // Euclidean Distance
#define COSINE 0x01  // Cosine Similarity

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
    INVALID_ID,
    INVALID_REF,
    DUPLICATED_ENTRY,
    NOT_FOUND_ID,
    INDEX_EMPTY,
    THREAD_ERROR,
    SYSTEM_ERROR,
} ErrorCode;

/**
 * Constants for index types.
 */
#define FLAT_INDEX    0x00  // Sequential flat index (single-threaded)
#define FLAT_INDEX_MP 0x01  // Flat index with multi-threaded support
#define NSW_INDEX     0x02  // Navigable Small World graph (planned)
#define HNSW_INDEX    0x03  // Hierarchical NSW (planned)

/**
 * Statistics structure for timing measurements.
 */
typedef struct {
    uint64_t count;      // Number of operations
    double   total;      // Total time in seconds
    double   min;        // Minimum operation time
    double   max;        // Maximum operation time
} TimeStat;

/**
 * Aggregate statistics for the index.
 */
typedef struct {
    TimeStat insert;     // Insert operations timing
    TimeStat delete;     // Delete operations timing
    TimeStat search;     // Single search timing
    TimeStat search_n;   // Multi-search timing
} IndexStats;

/**
 * Structure representing a single node in the hash map.
 * Each node stores a 64-bit unique ID, a reference to a index struct (ref),
 * and a pointer to the next node in the same bucket.
 */
typedef struct map_node {
    uint64_t id;
    void   *ref;
    struct map_node *next;
} MapNode;

/**
 * Structure representing the hash map.
 * Includes configuration for load factor threshold, total map size,
 * number of elements currently inserted, and the map buckets.
 */
typedef struct {
    uint16_t lfactor;             // Current load factor
    uint16_t lfactor_thrhold;     // Load factor threshold for triggering rehash
    uint32_t mapsize;             // Total number of buckets

    uint64_t elements;            // Total number of elements stored
    MapNode  **map;               // Array of buckets
} Map;


/**
 * Structure representing an abstract index for vector search.
 * It supports multiple indexing strategies through function pointers.
 */
typedef struct {
    char *name;        // Name of the indexing method (e.g., "Flat", "HNSW")
    void *data;        // Pointer to the specific index data structure
    void *context;     // Additional context for advanced indexing needs

    IndexStats stats;  // Accumulated timing statistics for operations

    Map map;           // ID-to-node hash map used by all index types

    pthread_rwlock_t rwlock; // Read-write lock for thread-safe access

    /**
     * Searches for the `n` closest matches to the given vector.
     * @param data The specific index data structure.
     * @param vector The input vector.
     * @param dims The number of dimensions.
     * @param results Output array to store the closest matches.
     * @param n The number of matches to retrieve.
     * @return The number of matches found, or -1 on error.
     */
    int (*search_n)(void *, float32_t *, uint16_t, MatchResult *, int);

    /**
     * Searches for the best match to the given vector.
     * @param data The specific index data structure.
     * @param vector The input vector.
     * @param dims The number of dimensions.
     * @param result Output structure to store the best match.
     * @return 0 if successful, or -1 on error.
     */
    int (*search)(void *, float32_t *, uint16_t, MatchResult *);

    /**
     * Inserts a new vector into the index.
     * @param data The specific index data structure.
     * @param id The unique identifier for the vector.
     * @param vector The input vector.
     * @param dims The number of dimensions.
     * @param ref Optional output pointer to store the internal reference.
     * @return 0 if successful, or -1 on error.
     */
    int (*insert)(void *, uint64_t, float32_t *, uint16_t, void **ref);

    /**
     * Deletes a vector from the index using its ID.
     * @param data The specific index data structure.
     * @param ref Internal reference to the vector (retrieved via map).
     * @return 0 if successful, or -1 on error.
     */
    int (*delete)(void *, void *);

    /**
     * Releases internal resources allocated by the index (if any).
     * @param ref Double pointer to the data/context to release.
     * @return 0 if successful, or -1 on error.
     */
    int (*_release)(void **);

} Index;

#ifndef _LIB_CODE
/**
 * Returns the version string of the library.
 */
extern const char *__LIB_VERSION();

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
 * Retrieves the internal statistics of the index.
 */
extern int stats(Index *Index, IndexStats *stats);

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
 * Releases all resources associated with the index.
 * @param index Double pointer to the index to be destroyed.
 * @return 0 if successful, or -1 on error.
 */
extern int destroy_index(Index **index);
#endif

#endif // __VICTOR_H


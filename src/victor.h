/*
* index.h - Index Structure and Management for Vector Database
* 
* Copyright (C) 2025 Emiliano A. Billi
*
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
    int id;
    float32_t distance;
} MatchResult;

/**
 * Enumeration of available comparison methods.
 */
#define L2NORM 0x00  // Euclidean Distance
#define COSINE 0x01  // Cosine Similarity


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

#define FLAT_INDEX    0x00
#define FLAT_INDEX_MP 0x01
#define NSW_INDEX     0x02 // Not implemented yet
#define HNSW_INDEX    0x03 // Not implemented yet

typedef struct {
    uint64_t count;
    double   total;
    double   min;
    double   max;
} TimeStat;

typedef struct {
    TimeStat insert;
    TimeStat delete;
    TimeStat search;
    TimeStat search_n;
} IndexStats;

/**
 * Structure representing a single node in the hash map.
 * Each node stores a 64-bit unique ID, a reference to a value (ref),
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

	IndexStats stats;

	Map map;

	pthread_rwlock_t rwlock; // Read-write lock for thread safety

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
     * @return 0 if successful, or -1 on error.
     */
    int (*insert)(void *, uint64_t, float32_t *, uint16_t, void **ref);

    /**
     * Deletes a vector from the index using its ID.
     * @param data The specific index data structure.
     * @param id The unique identifier of the vector to delete.
     * @return 0 if successful, or -1 on error.
     */
    int (*delete)(void *, void *);

    int (*_release)(void **);

} Index;

#ifndef _LIB_CODE
extern const char *__LIB_VERSION();

/**
 * Wrapper functions to call the corresponding method in `Index`.
 * These functions ensure safe access and provide a unified interface.
 */
extern int search_n(Index *index, float32_t *vector, uint16_t dims, MatchResult *results, int n);
extern int search(Index *index, float32_t *vector, uint16_t dims, MatchResult *result);
extern int insert(Index *index, uint64_t id, float32_t *vector, uint16_t dims);
extern int delete(Index *index, uint64_t id);

extern int stats(Index *Index, IndexStats *stats);

extern Index *alloc_index(int type, int method, uint16_t dims, void *icontext);
extern int destroy_index(Index **index);
#endif

#endif // __VICTOR_H

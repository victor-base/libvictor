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

#ifndef _INDEX_H
#define _INDEX_H

#include "victor.h"
#include "map.h"

#define __LIB_VERSION_MAJOR "1"
#define __LIB_VERSION_MINOR "0"
#define __LIB_VERSION_PATCH "1"


#if defined(_WIN32) || defined(_WIN64)
    #define OS "Windows"
#elif defined(__APPLE__) || defined(__MACH__)
    #define OS "macOS"
#elif defined(__linux__)
    #define OS "Linux"
#elif defined(__unix__)
    #define OS "Unix"
#elif defined(__ANDROID__)
    #define OS "Android"
#else
    #define OS "Unknown OS"
#endif

#if defined(__x86_64__) || defined(_M_X64)
    #define ARCH "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define ARCH "ARM64"
#elif defined(__arm__) || defined(_M_ARM)
    #define ARCH "ARM"
#elif defined(__i386__) || defined(_M_IX86)
    #define ARCH "x86"
#else
    #define ARCH "Unknown Arch"
#endif

/**
 * Structure representing an abstract index for vector search.
 * It supports multiple indexing strategies through function pointers.
 */
typedef struct Index {
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
    int (*release)(void **);

} Index;

#endif

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
#include "store.h"
#include "map.h"
#include "version.h"


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
	int  method;
    IndexStats stats;  // Accumulated timing statistics for operations

    Map map;           // ID-to-node hash map used by all index types

    pthread_rwlock_t rwlock; // Read-write lock for thread-safe access

    /**
     * Searches for the `n` closest matches to the given vector with filtering.
     * 
     * This function performs approximate nearest neighbor search similar to search_n,
     * but applies an additional filter to exclude certain vectors from the results.
     * The filter mechanism is implementation-specific and may vary between index types.
     * 
     * @param data The specific index data structure.
	 * @param tag Filter value used to exclude certain vectors from search results.
     * @param vector The input vector for which to find nearest neighbors.
     * @param dims The number of dimensions in the input vector.
     * @param results Output array to store the filtered closest matches.
     * @param n The maximum number of matches to retrieve.
     * @return The number of matches found (0 to n), or negative error code on failure.
     */
	int (*search) (void*, uint64_t, float32_t *, uint16_t, MatchResult *, int);

    /**
     * Inserts a new vector into the index.
     * @param data The specific index data structure.
     * @param id The unique identifier for the vector.
     * @param vector The input vector.
     * @param dims The number of dimensions.
     * @param ref Optional output pointer to store the internal reference.
     * @return 0 if successful, or -1 on error.
     */
    int (*insert)(void *data, uint64_t id, uint64_t tag, float32_t *vector, uint16_t dims, void **ref);

	/**
     * Updates the internal context of the index.
     * @param data The specific index data structure.
     * @param context The new context or configuration to apply.
     * @param type The type of update to perform.
     * @return SUCCESS on success, or an error code on failure.
     */
	int (*update_icontext)(void *data, void *context, int type);

	/**
     * Remaps the internal structure of the index using a new map.
     * @param data The specific index data structure.
     * @param map The new map to use for remapping.
     * @return SUCCESS on success, or an error code on failure.
     */
    int (*remap)(void *data, Map *map);


	int (*set_tag) (void *data, void *ref, uint64_t tag);

	/**
     * Compares a vector with a node in the index.
     * @param data The specific index data structure.
     * @param node The node to compare.
     * @param vector The input vector.
     * @param dimd The number of dimensions.
     * @param distance Output pointer to store the computed distance.
     * @return SUCCESS on success, or an error code on failure.
     */
	int (*compare)(void *data, const void *node, float32_t *vector, uint16_t dims, float32_t *distance);

    /**
     * Deletes a vector from the index using its ID.
     * @param data The specific index data structure.
     * @param ref Internal reference to the vector (retrieved via map).
     * @return 0 if successful, or -1 on error.
     */
    int (*delete)(void *, void *);

    /**
     * Serializes the current state of the index and writes it to disk.
     *
     * This function creates a persistent on-disk representation of the index,
     * including vector data, internal structures (e.g., graph or flat layout),
     * and metadata required for reconstruction via a future load.
     *
     * The actual format and implementation are index-specific.
     *
     * @param data The specific index data structure (e.g., Flat, NSW, HNSW).
     * @param filename Path to the file where the index should be dumped.
     * @return 0 if successful, or -1 on error.
     */
    int (*dump)(void *data, IOContext *io);

    /**
     * Exports the index data in a portable format.
     * 
     * This function creates an export of the index that can be transferred
     * between different systems or used for backup purposes. Unlike dump(),
     * which creates a native binary format, export() typically creates a
     * more portable representation that may be platform-independent.
     * 
     * @param data The specific index data structure to export.
     * @param io IOContext for writing the exported data.
     * @return SUCCESS on successful export, or error code on failure.
     */
	int (*export)(void *data, IOContext *io);

    /**
     * Imports index data from a previously exported format.
     * 
     * This function reconstructs an index from data that was previously
     * exported using the export() function. It can handle different import
     * modes and uses the provided map for ID-to-reference mapping.
     * 
     * @param data The index data structure to populate with imported data.
     * @param io IOContext for reading the import data.
     * @param map Map structure for ID-to-reference mapping during import.
     * @param mode Import mode specifying how to handle the import process.
     * @return SUCCESS on successful import, or error code on failure.
     */
	int (*import)(void *data, IOContext *io, Map *map, int mode);

    /**
     * Releases internal resources allocated by the index (if any).
     * @param ref Double pointer to the data/context to release.
     * @return 0 if successful, or -1 on error.
     */
    int (*release)(void **);

} Index;

#endif

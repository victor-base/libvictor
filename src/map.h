/*
* map.h - Hash Map Structure for Vector Indexing
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
* Contact: emiliano.billi@gmail.com
*
* Purpose:
* This header file defines a generic hash map structure used to store references
* to vector objects indexed by 64-bit unique IDs. The structure supports dynamic
* resizing (rehashing) based on a configurable load factor threshold, providing
* efficient insertions, lookups, and deletions.
*/

#ifndef __MAP_H
#define __MAP_H 1

#include "victor.h"
#include "panic.h"
#include "mem.h"


/**
 * Structure representing a single node in the hash map.
 * Each node stores a 64-bit unique ID, a reference to a index struct (ref),
 * and a pointer to the next node in the same bucket.
 */
typedef struct map_node {
    uint64_t key;
    uint64_t value;
    struct map_node *next;
} MapNode;


_Static_assert(sizeof(uintptr_t) <= sizeof(uint64_t), "Pointers won't fit in uint64_t");


/**
 * Structure representing the hash map.
 * Includes configuration for load factor threshold, total map size,
 * number of elements currently inserted, and the map buckets.
 */
typedef struct {
    uint16_t lfactor_thrhold;     // Load factor threshold for triggering rehash
    uint32_t mapsize;             // Total number of buckets

    uint64_t elements;            // Total number of elements stored
    MapNode  **map;               // Array of buckets
} Map;

#define MAP_INIT() ((Map){ \
    .lfactor_thrhold = 0, \
    .mapsize = 0, \
    .elements = 0, \
    .map = NULL \
})

/**
 * Checks whether the specified ID exists in the map.
 *
 * @param map Pointer to the Map structure.
 * @param id  ID to search for.
 * @return 1 if ID exists, 0 otherwise.
 */
extern int map_has(const Map *map, uint64_t key);

/**
 * Checks whether the specified ID exists in the map and get it.
 *
 * @param map Pointer to the Map structure.
 * @param id  ID to search for.
 * @return pointer to the ref or null
 */
extern uint64_t map_get(const Map *map, uint64_t key);
extern void *map_get_p(const Map *map, uint64_t key);
extern int map_get_safe(const Map *map, uint64_t key, uint64_t *out);
extern int map_get_safe_p(const Map *map, uint64_t key, void **out);

/**
 * Removes an entry with the given ID from the map.
 *
 * @param map Pointer to the Map structure.
 * @param id  ID of the entry to remove.
 * @return Pointer to the removed reference, or NULL if not found.
 */
extern uint64_t map_remove(Map *map, uint64_t key);
extern void *map_remove_p(Map *map, uint64_t key);
extern int map_remove_safe(Map *map, uint64_t key, uint64_t *out);
extern int map_remove_safe_p(Map *map, uint64_t key, void **out);
/**
 * Inserts a new entry into the map.
 * Triggers a rehash if the load factor exceeds the threshold.
 *
 * @param map Pointer to the Map structure.
 * @param id  Unique 64-bit ID for the new entry.
 * @param ref Pointer to the reference to store.
 * @return SUCCESS on success, error code otherwise.
 */
extern int map_insert(Map *map, uint64_t key, uint64_t value);
extern int map_insert_p(Map *map, uint64_t key, void *value);
/**
 * Initializes the map with a given initial size and load factor threshold.
 *
 * @param map                Pointer to the Map structure.
 * @param initial_size       Initial number of buckets.
 * @param lfactor_thrhold    Load factor threshold for rehashing.
 * @return SUCCESS on success, error code otherwise.
 */
extern int init_map(Map *map, uint32_t initial_size, uint16_t lfactor_thrhold);

/**
 * Reset the map
 */
extern void map_purge(Map *map);

/**
 * Destroys the map and frees all allocated memory.
 *
 * @param map Pointer to the Map structure to destroy.
 */
extern void map_destroy(Map *map);

#define MAP_KEY_NOT_FOUND -1
#define MAP_OK 0

typedef enum {
    MAP_SUCCESS = 0,             // Operación exitosa
    MAP_ERROR_ALLOC = -1,        // Error de memoria (calloc/realloc)
} MapErrorCode;

#endif // __MAP_H

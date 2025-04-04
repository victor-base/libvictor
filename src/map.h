/*
* map.h - Hash Map Structure for Vector Indexing
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
 * Checks whether the specified ID exists in the map.
 *
 * @param map Pointer to the Map structure.
 * @param id  ID to search for.
 * @return 1 if ID exists, 0 otherwise.
 */
extern int map_has(const Map *map, uint64_t id);

/**
 * Checks whether the specified ID exists in the map and get it.
 *
 * @param map Pointer to the Map structure.
 * @param id  ID to search for.
 * @return pointer to the ref or null
 */
extern void *map_ref(const Map *map, uint64_t id);

/**
 * Removes an entry with the given ID from the map.
 *
 * @param map Pointer to the Map structure.
 * @param id  ID of the entry to remove.
 * @return Pointer to the removed reference, or NULL if not found.
 */
extern void *map_remove(Map *map, uint64_t id);

/**
 * Inserts a new entry into the map.
 * Triggers a rehash if the load factor exceeds the threshold.
 *
 * @param map Pointer to the Map structure.
 * @param id  Unique 64-bit ID for the new entry.
 * @param ref Pointer to the reference to store.
 * @return SUCCESS on success, error code otherwise.
 */
extern int map_insert(Map *map, uint64_t id, void *ref);

/**
 * Initializes the map with a given initial size and load factor threshold.
 *
 * @param map                Pointer to the Map structure.
 * @param initial_size       Initial number of buckets.
 * @param lfactor_thrhold    Load factor threshold for rehashing.
 * @return SUCCESS on success, error code otherwise.
 */
extern int map_init(Map *map, uint32_t initial_size, uint16_t lfactor_thrhold);

/**
 * Destroys the map and frees all allocated memory.
 *
 * @param map Pointer to the Map structure to destroy.
 */
extern void map_destroy(Map *map);

#endif // __MAP_H

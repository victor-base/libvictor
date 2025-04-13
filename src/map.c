/*
* map.c - Hash Map Implementation for Vector Indexing
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
* Contact: emiliano.billi@gmail.com
*
* Purpose:
* This file implements a basic hash map used to associate 64-bit IDs
* with references to arbitrary objects. It supports efficient lookups,
* insertions with automatic rehashing, and deletion of entries.
*/

#include "map.h"

#define LOAD_FACTOR(map) \
	((map)->mapsize == 0 ? 0 : ((map)->elements) / (map)->mapsize)

/**
 * Computes the bucket index for a given ID using modulo hashing.
 */
static inline uint32_t map_hash(const Map *map, uint64_t id) {
	PANIC_IF(map == NULL, "map null in map_hash");
	return id % map->mapsize;
}

/**
 * Checks whether an entry with the given ID exists in the map.
 */
int map_has(const Map *map, uint64_t id) {
	int i = map_hash(map, id);
	MapNode *ptr;

	PANIC_IF(map == NULL, "map null in map_has");

	ptr = map->map[i];
	while (ptr) {
		if (ptr->id == id)
			return 1;
		ptr = ptr->next;
	}
	return 0;
}

/**
 * Checks whether an entry with the given ID exists in the map and retun ref pointer.
 */
void *map_ref(const Map *map, uint64_t id) {
	int i = map_hash(map, id);
	MapNode *ptr;

	PANIC_IF(map == NULL, "map null in map_has");

	ptr = map->map[i];
	while (ptr) {
		if (ptr->id == id)
			return ptr->ref;
		ptr = ptr->next;
	}
	return NULL;
}

/**
 * Removes and returns the reference associated with the given ID.
 */
void *map_remove(Map *map, uint64_t id) {
	PANIC_IF(map == NULL, "map null in map_remove");

	int i = map_hash(map, id);
	MapNode **pp = &map->map[i];
	MapNode *node;

	while ((node = *pp)) {
		if (node->id == id) {
			*pp = node->next;
			void *ref = node->ref;
			free_mem(node);
			map->elements--;
			return ref;
		}
		pp = &node->next;
	}

	return NULL;
}

/**
 * Internal function that resizes the hash map and re-distributes entries.
 */
static int map_rehash(Map *map, uint32_t new_mapsize) {
	MapNode *next, *curr;
	uint32_t j;

	PANIC_IF(map == NULL, "map is null in rehash");

	MapNode **new_map = (MapNode **) calloc_mem(new_mapsize, sizeof(MapNode*));
	if (new_map == NULL)
		return MAP_ERROR_ALLOC;

	for (uint32_t i = 0; i < map->mapsize; ++i) {
		curr = map->map[i];
		while (curr) {
			next = curr->next;
			j = curr->id % new_mapsize;
			curr->next = new_map[j];
			new_map[j] = curr;
			curr = next;
		}
	}

	free_mem(map->map);
	map->map = new_map;
	map->mapsize = new_mapsize;

	return MAP_SUCCESS;
}

/**
 * Inserts a new entry into the map, with automatic rehashing if needed.
 */
int map_insert(Map *map, uint64_t id, void *ref) {
	PANIC_IF(map == NULL, "map null in map_insert");
	PANIC_IF(map->mapsize == 0, "map has invalid mapsize in insert");

	if (LOAD_FACTOR(map) > map->lfactor_thrhold) {
		uint32_t new_size = map->mapsize * 2;
		if (map_rehash(map, new_size) != MAP_SUCCESS)
			return MAP_ERROR_ALLOC;
	}

	int i = map_hash(map, id);

	MapNode *node = (MapNode *) calloc_mem(1, sizeof(MapNode));
	if (!node)
		return MAP_ERROR_ALLOC;

	node->id = id;
	node->ref = ref;
	node->next = map->map[i];
	map->map[i] = node;

	map->elements++;

	return MAP_SUCCESS;
}

/**
 * Initializes the map with a specified size and load factor threshold.
 */
int init_map(Map *map, uint32_t initial_size, uint16_t lfactor_thrhold) {
	if (!map || initial_size == 0)
		return INVALID_INIT;

	map->map = (MapNode **) calloc_mem(initial_size, sizeof(MapNode*));
	if (!map->map)
		return MAP_ERROR_ALLOC;

	map->mapsize = initial_size;
	map->lfactor = 0;
	map->lfactor_thrhold = lfactor_thrhold;
	map->elements = 0;

	return MAP_SUCCESS;
}

/**
 * Destroys the map and frees all memory associated with it.
 */
void map_destroy(Map *map) {
	if (!map || !map->map)
		return;

	for (uint32_t i = 0; i < map->mapsize; ++i) {
		MapNode *node = map->map[i];
		while (node) {
			MapNode *next = node->next;
			free_mem(node);
			node = next;
		}
	}

	free_mem(map->map);
	map->map = NULL;
	map->elements = 0;
	map->mapsize = 0;
}
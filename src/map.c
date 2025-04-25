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


/**
 * Computes the bucket index for a given ID using modulo hashing.
 */
static inline uint32_t map_hash(const Map *map, uint64_t key) {
	PANIC_IF(map == NULL, "map null in map_hash");
	return key % map->mapsize;
}

static inline int LOAD_FACTOR(const Map *map) {
	return map->mapsize == 0 ? 0 : map->elements / map->mapsize;
}

/**
 * Checks whether an entry with the given key exists in the map.
 */
int map_has(const Map *map, uint64_t key) {
	int i = map_hash(map, key);
	MapNode *ptr;

	ptr = map->map[i];
	while (ptr) {
		if (ptr->key == key)
			return 1;
		ptr = ptr->next;
	}
	return 0;
}

/**
 * Checks whether an entry with the given key exists in the map and retun ref pointer.
 */
uint64_t map_get(const Map *map, uint64_t key) {
    uint64_t value;
    return map_get_safe(map, key, &value) == MAP_OK ? value : 0;
}


void *map_get_p(const Map *map, uint64_t key) {
    void *ptr;
    return map_get_safe_p(map, key, &ptr) == MAP_OK ? ptr : NULL;
}

int map_get_safe(const Map *map, uint64_t key, uint64_t *out) {
    int i = map_hash(map, key);
    MapNode *ptr = map->map[i];

    while (ptr) {
        if (ptr->key == key) {
            *out = ptr->value;
            return MAP_OK;
        }
        ptr = ptr->next;
    }

    return MAP_KEY_NOT_FOUND;
}

int map_get_safe_p(const Map *map, uint64_t key, void **out) {
    uint64_t value;
    int result = map_get_safe(map, key, &value);
    if (result != MAP_OK) return result;
    *out = (void *)(uintptr_t) value;
    return MAP_OK;
}

int map_remove_safe(Map *map, uint64_t key, uint64_t *out) {
    int i = map_hash(map, key);
    MapNode **pp = &map->map[i];
    MapNode *node;

    while ((node = *pp)) {
        if (node->key == key) {
            *pp = node->next;
            *out = node->value;
            free_mem(node);
            map->elements--;
            return MAP_OK;
        }
        pp = &node->next;
    }

    return MAP_KEY_NOT_FOUND;
}

int map_remove_safe_p(Map *map, uint64_t key, void **out) {
    uint64_t value;
    int result = map_remove_safe(map, key, &value);
    if (result != MAP_OK) return result;
    *out = (void *)(uintptr_t) value;
    return MAP_OK;
}


/**
 * Removes and returns the reference associated with the given key.
 */
uint64_t map_remove(Map *map, uint64_t key) {
    uint64_t value;
    return map_remove_safe(map, key, &value) == MAP_OK ? value : 0;
}

void *map_remove_p(Map *map, uint64_t key) {
    void *ptr;
    return map_remove_safe_p(map, key, &ptr) == MAP_OK ? ptr : NULL;
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
			j = curr->key % new_mapsize;
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
int map_insert(Map *map, uint64_t key, uint64_t value) {
	PANIC_IF(map == NULL, "map null in map_insert");
	PANIC_IF(map->mapsize == 0, "map has invalid mapsize in insert");

	if (LOAD_FACTOR(map) > map->lfactor_thrhold) {
		uint32_t new_size = map->mapsize * 2;
		if (map_rehash(map, new_size) != MAP_SUCCESS)
			return MAP_ERROR_ALLOC;
	}

	int i = map_hash(map, key);

	MapNode *node = (MapNode *) calloc_mem(1, sizeof(MapNode));
	if (!node)
		return MAP_ERROR_ALLOC;

	node->key = key;
	node->value = value;
	node->next = map->map[i];
	map->map[i] = node;

	map->elements++;

	return MAP_SUCCESS;
}

int map_insert_p(Map *map, uint64_t key, void* value) {
	return map_insert(map, key, (uint64_t)(uintptr_t) value);
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
#include <stdint.h>
#include <pthread.h>
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxhash.h"
#include "victorkv.h"
#include "file.h"
#include "panic.h"
#include "mem.h"


#define DEFAULT_LOAD_FACTOR 15
#define DEFAULT_INIT_SIZE   100
#define MAX_NAME_LEN        150

/**
 * @brief Header structure stored at the beginning of the dump file.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;         /**< File format identifier. */
    uint8_t  major;         /**< Major version. */
    uint8_t  minor;         /**< Minor version. */
    uint8_t  patch;         /**< Patch version. */
	uint8_t  _res[5];
	uint32_t elements;
} kv_store_header_t;

typedef struct {
	uint64_t entry_offset;
	uint64_t entry_size;
} kv_store_entry_t;

typedef struct {
    uint64_t hash;     
    uint32_t klen;     
    uint32_t vlen;
    uint8_t  buff[];
} kvEntry;
#pragma pack(pop)

typedef struct kv_node {
	kvEntry *entry;
	struct kv_node *next;
	struct kv_node *prev;
} kvNode;

typedef struct {
	int elements;
	kv_store_entry_t *io_entry_index;
	kvEntry **io_entry;
} kv_io_t;

/**
 * Structure representing the hash map.
 * Includes configuration for load factor threshold, total map size,
 * number of elements currently inserted, and the map buckets.
 */
typedef struct kvTable {
	char name[MAX_NAME_LEN];

	pthread_rwlock_t rwlock; // Read-write lock for thread-safe access

	uint16_t rehash;              // Rehahes
	uint16_t lfactor_thrhold;     // Load factor threshold for triggering rehash
    uint32_t mapsize;             // Total number of buckets
	
    uint64_t elements;            // Total number of elements stored
    kvNode   **map;               // Array of buckets
} kvTable;



static int kv_store_io_dump(kv_io_t *io, IOFile *file) {
	PANIC_IF(io == NULL, "invalid io pointer");
	PANIC_IF(file == NULL, "invalid file");
	kv_store_header_t header;
	int i;

	// Escribir reader

	if (file_write(&header, sizeof(kv_store_header_t), 1, file) != 1)
		return -1;

	for (i = 0; i < io->elements; i++)
		if (file_write(&io->io_entry_index[i], sizeof(kv_store_entry_t), 1, file) != 1)
			return -1;
	for (i = 0; i < io->elements; i++) {
		if (file_write(io->io_entry[i], io->io_entry_index[i].entry_size, 1, file) != 1)
			return -1;
	}
	return 0;
}

static int kvio_init(kv_io_t *io, int elements) {
	PANIC_IF(io == NULL, "invalid io pointer");
	PANIC_IF(elements <= 0, "invalid number of elements");

	memset(io, 0, sizeof(kv_io_t));
	io->elements = elements;
	io->io_entry_index = (kv_store_entry_t *)calloc_mem(elements, sizeof(kv_store_entry_t));
	if (!io->io_entry_index)
		return -1;
	io->io_entry = (kvEntry **)calloc_mem(elements, sizeof(kvEntry *));
	if (!io->io_entry) {
		free_mem(io->io_entry_index);
		return -1;
	}
	return 0;
}

static void kvio_free(kv_io_t **io) {
	if (!io || !*io)
		return;
	if((*io)->io_entry)
		free_mem((*io)->io_entry);
	if ((*io)->io_entry_index)
		free_mem((*io)->io_entry_index);
	*io = NULL;
}

static int kv_store_io_load(kv_io_t *io, IOFile *file) {
	PANIC_IF(io == NULL, "invalid io pointer");
	PANIC_IF(file == NULL, "invalid file");
	kv_store_header_t header;
	uint64_t offset;
	int i;

	if (file_read(&header, sizeof(kv_store_header_t), 1, file) != 1)
		return -1;

	if (kvio_init(io, header.elements) != 0)
		return -1;

	if (file_read(io->io_entry_index, 
				sizeof(kv_store_entry_t), 
				header.elements,file) != header.elements) {
		kvio_free(&io);
		return -1;
	}

	offset = sizeof(kv_store_header_t) + header.elements * sizeof(kv_store_entry_t);
	if (offset != io->io_entry_index[0].entry_offset || 
	   (uint64_t)offset != (uint64_t)file_tello(file)) {
		kvio_free(&io);
		return -1;
	}

	for (i = 0; i < (int)header.elements; i++) {
		PANIC_IF(io->io_entry_index[i].entry_size == 0, "invalid entry size");
		io->io_entry[i] = (kvEntry *)calloc_mem(1, io->io_entry_index[i].entry_size);
		if (!io->io_entry[i]) {
			i = i - 1;
			goto error_cleanup;
		}
		if (file_read(io->io_entry[i], io->io_entry_index[i].entry_size, 1, file) != 1)
			goto error_cleanup;
	}
	return 0;
error_cleanup:
	for (; i >= 0; i--)
		free_mem(io->io_entry[i]);
	kvio_free(&io);
	return -1;
}

static kvNode *get_node(kvTable *table, void *key, int klen) {
	if (!table || !key || klen < 0)
		return NULL;
	uint64_t hash = XXH64(key, klen, 0);
	int bucket = hash % table->mapsize;
	kvNode *node = table->map[bucket];
	while (node) {
		if (hash == node->entry->hash && 
			(uint32_t)klen == node->entry->klen && 
			memcmp(node->entry->buff, key, klen) == 0)
			return node;
		node = node->next;
	}
	return NULL;
}

/**
 * @brief Retrieves the value associated with a given key from the hash map.
 *
 * This function searches for a key in the hash map and, if found, returns a pointer to the
 * associated value and its length. The value is not copied; instead, a pointer to the internal
 * buffer is provided.
 *
 * Thread-safety is ensured by acquiring a read lock during the lookup.
 *
 * @param table Pointer to the hash map (kvTable).
 * @param key Pointer to the key to look up.
 * @param klen Length of the key in bytes.
 * @param value Output pointer to the value's memory location within the entry buffer.
 * @param vlen Output pointer to store the length of the value in bytes.
 *
 * @return KV_SUCCESS (0) if the key was found.
 *         KV_ERROR_INVALID_table if the table is NULL.
 *         KV_ERROR_INVALID_KEY if the key is NULL or has invalid length.
 *         KV_ERROR_INVALID_VALUE if value or vlen output pointers are NULL.
 *         KV_KEY_NOT_FOUND if the key does not exist in the map.
 *
 * @note The value returned via `*value` is a pointer to internal memory; the caller must not free it.
 * @note The function does not copy the value; it only provides direct access to the internal storage.
 */
int kv_get(kvTable *table, void *key, int klen, void **value, int *vlen) {
	if (!table) 
		return KV_ERROR_INVALID_TABLE;
	if (!key || klen <= 0)
		return KV_ERROR_INVALID_KEY;
	if (!value || !vlen)
		return KV_ERROR_INVALID_VALUE;

	pthread_rwlock_rdlock(&table->rwlock);
	kvNode *node = get_node(table, key, klen);
	pthread_rwlock_unlock(&table->rwlock);
	if (node) {
		*value = &node->entry->buff[node->entry->klen];
		*vlen  = node->entry->vlen;
		return KV_SUCCESS; 
	}
	return KV_KEY_NOT_FOUND;
}

/**
 * @brief Retrieves a copy of the value associated with a given key from the hash map.
 *
 * This function searches for a key in the hash map and, if found, allocates a new buffer,
 * copies the value into it, and returns a pointer to this buffer along with its length.
 * The caller is responsible for freeing the returned buffer.
 *
 * Thread-safety is ensured by acquiring a read lock during the lookup.
 *
 * @param table Pointer to the hash map (kvTable).
 * @param key Pointer to the key to look up.
 * @param klen Length of the key in bytes.
 * @param value Output pointer to a newly allocated buffer containing the value.
 * @param vlen Output pointer to store the length of the value in bytes.
 *
 * @return KV_SUCCESS (0) if the key was found and the value copied.
 *         KV_ERROR_INVALID_table if the table is NULL.
 *         KV_ERROR_INVALID_KEY if the key is NULL or has invalid length.
 *         KV_ERROR_INVALID_VALUE if value or vlen output pointers are NULL.
 *         KV_ERROR_SYSTEM if memory allocation for the value copy fails.
 *         KV_KEY_NOT_FOUND if the key does not exist in the map.
 *
 * @note The value returned via `*value` must be freed by the caller using free_mem().
 * @note The function copies the value, so the caller receives an independent buffer.
 */
int kv_get_copy(kvTable *table, void *key, int klen,void **value, int *vlen) {
	if (!table) 
		return KV_ERROR_INVALID_TABLE;
	if (!key || klen <= 0)
		return KV_ERROR_INVALID_KEY;
	if (!value || !vlen)
		return KV_ERROR_INVALID_VALUE;

	pthread_rwlock_rdlock(&table->rwlock);
	kvNode *node = get_node(table, key, klen);
	if (node) {
		*value = global_calloc_mem(1, node->entry->vlen);
		if (!*value) {
			pthread_rwlock_unlock(&table->rwlock);
			return KV_ERROR_SYSTEM;
		}
		memcpy(*value, &node->entry->buff[node->entry->klen],node->entry->vlen);
		*vlen  = node->entry->vlen;
		pthread_rwlock_unlock(&table->rwlock);
		return KV_SUCCESS; 
	} else {
		*value = NULL;
	}
	pthread_rwlock_unlock(&table->rwlock);
	return KV_KEY_NOT_FOUND;
}


/**
 * @brief Deletes a key-value pair from the hash map.
 *
 * This function searches for a key in the hash map and removes the corresponding entry
 * if found. It safely unlinks the node from its bucket's linked list and releases the
 * memory associated with the key-value entry.
 *
 * Thread-safety is ensured by acquiring a write lock during the deletion process.
 *
 * @param table Pointer to the hash map (kvTable).
 * @param key Pointer to the key to delete.
 * @param klen Length of the key in bytes.
 *
 * @return KV_SUCCESS (0) if the entry was successfully removed.
 *         KV_ERROR_INVALID_table if the table is NULL.
 *         KV_ERROR_INVALID_KEY if the key is NULL or has invalid length.
 *         KV_KEY_NOT_FOUND if the key does not exist in the map.
 *
 * @note The function will release all memory associated with the removed entry.
 * @note It is safe to call this function while other threads are performing reads.
 */
int kv_del(kvTable *table, void *key, int klen) {
    if (!table)
		return KV_ERROR_INVALID_TABLE;
	if (!key || klen <= 0)
        return KV_ERROR_INVALID_KEY;

	pthread_rwlock_wrlock(&table->rwlock);
    kvNode *node = get_node(table, key, klen);
    if (!node) {
		pthread_rwlock_unlock(&table->rwlock);
		return KV_KEY_NOT_FOUND;
	}
	PANIC_IF(node->entry == NULL, "invalid entry");

    int bucket = node->entry->hash % table->mapsize;

    if (table->map[bucket] == node)
        table->map[bucket] = node->next;

    if (node->prev)
        node->prev->next = node->next;

    if (node->next)
        node->next->prev = node->prev;

    free_mem(node->entry);
    free_mem(node);

    table->elements--;
	pthread_rwlock_unlock(&table->rwlock);
    return KV_SUCCESS;
}

/**
 * @brief Rehashes the hash map to a new size.
 *
 * This function redistributes all existing entries in the hash map to a new bucket array
 * of size `nsize`. It is typically called when the current load factor exceeds a predefined
 * threshold, in order to maintain efficient access times. The hash of each entry is reused
 * to compute the new bucket table modulo the new size.
 *
 * @param table Pointer to the hash map structure (`kvTable`).
 * @param nsize New size of the hash map (must be greater than current size).
 *
 * @note This function assumes that the caller has acquired a write lock (`rwlock`)
 *       on the table if concurrent access is expected.
 *
 * @return KV_SUCCESS on success.
 *         KV_ERROR_SYSTEM if memory allocation for the new bucket array fails.
 *
 * @warning The original map is freed and replaced with the new one. All internal node pointers
 *          are adjusted accordingly. This function does not modify the number of elements.
 */
static int rehash(kvTable *table, uint32_t nsize) {
	int bucket;
	PANIC_IF(table == NULL, "invalid table parameter");
	PANIC_IF(nsize == 0 || nsize < table->mapsize, "invalid size parameter");

	kvNode **tmp = (kvNode **) calloc_mem(nsize, sizeof(kvNode*));
	if (!tmp)
		return KV_ERROR_SYSTEM;

	for (uint32_t i = 0; i < table->mapsize; ++i) {
		kvNode *ptr;
		while (table->map[i]) {
			ptr = table->map[i];
			table->map[i] = ptr->next;
			ptr->next = NULL;
			ptr->prev = NULL;
			bucket = ptr->entry->hash % nsize;
			if (tmp[bucket]) {
				tmp[bucket]->prev = ptr;
				ptr->next = tmp[bucket];
			}
			tmp[bucket] = ptr;
		}
	}
    free_mem(table->map);
    table->map = tmp;
    table->mapsize = nsize;
    table->rehash++;
    return KV_SUCCESS;
}

/**
 * @brief Inserts or updates a key-value pair in the hash map.
 *
 * This function inserts a new entry or updates an existing one in the hash map represented
 * by `kvTable`. If the key already exists, the value is updated in-place (reallocating memory
 * if necessary). If the key does not exist, a new node is created and inserted into the corresponding
 * bucket based on the key's hash.
 *
 * The function also performs rehashing if the load factor exceeds the configured threshold,
 * and ensures thread-safety using a write lock.
 *
 * @param table Pointer to the hash map (kvTable).
 * @param key Pointer to the key to insert.
 * @param klen Length of the key in bytes.
 * @param value Pointer to the value to insert.
 * @param vlen Length of the value in bytes.
 *
 * @return KV_SUCCESS (0) on success.
 *         KV_ERROR_INVALID_table if the table is NULL.
 *         KV_ERROR_INVALID_KEY if the key is NULL or key length is invalid.
 *         KV_ERROR_INVALID_VALUE if the value is NULL or value length is invalid.
 *         KV_ERROR_SYSTEM if memory allocation fails or other internal errors occur.
 *
 * @note If the key exists and the new value is larger than the previously allocated space,
 *       the entry is reallocated.
 *
 * @note The key and value are stored contiguously in memory in the `buff` field of the entry.
 *
 * @note The function must be called with valid memory for key and value; it does not duplicate
 *       or validate the content beyond size and NULL checks.
 */
int kv_put(kvTable *table, void *key, int klen, void *value, int vlen) {
	kvEntry *tmp;
	kvNode *node;
	int bucket;
	if (!table)
		return KV_ERROR_INVALID_TABLE;
	if (!key || klen <= 0)
		return KV_ERROR_INVALID_KEY;
	if (!value || vlen <= 0)
		return KV_ERROR_INVALID_VALUE;
	
	pthread_rwlock_wrlock(&table->rwlock);

	if (table->mapsize > 0 && (table->elements / table->mapsize) > table->lfactor_thrhold) {
		if (rehash(table, table->mapsize * 2) != KV_SUCCESS) {
			pthread_rwlock_unlock(&table->rwlock);
			return KV_ERROR_SYSTEM;
		}
	}


	node = get_node(table, key, klen);
	if (node) {
		if (node->entry->vlen < (uint32_t)vlen) {
			tmp = (kvEntry *) realloc_mem(node->entry, sizeof(kvEntry) + klen + vlen);
			if (!tmp) {
				pthread_rwlock_unlock(&table->rwlock);
				return KV_ERROR_SYSTEM;
			}
			node->entry = tmp;
		} 
		memcpy(&node->entry->buff[node->entry->klen], value, vlen);
		node->entry->vlen = vlen;
		pthread_rwlock_unlock(&table->rwlock);
		return KV_SUCCESS;
	}
	tmp = (kvEntry *) calloc_mem(1, sizeof(kvEntry) + klen + vlen);
	if (!tmp) {
		pthread_rwlock_unlock(&table->rwlock);
		return KV_ERROR_SYSTEM;
	}
	tmp->hash = XXH64(key, klen, 0);
	tmp->klen = klen;
	tmp->vlen = vlen;
	memcpy(tmp->buff, key, klen);
	memcpy(&tmp->buff[klen], value, vlen);

	node = (kvNode *) calloc_mem(1, sizeof(kvNode));
	if (!node) {
		free_mem(tmp);
		pthread_rwlock_unlock(&table->rwlock);
		return KV_ERROR_SYSTEM;
	}
	bucket = tmp->hash % table->mapsize;
	node->entry = tmp;
	node->next = table->map[bucket];
	node->prev = NULL;
	if (node->next)
		node->next->prev = node;
	table->map[bucket] = node;
	table->elements++;
	pthread_rwlock_unlock(&table->rwlock);
	return KV_SUCCESS;
}

/**
 * @brief Allocates and initializes a new key-value table (hash map).
 *
 * This function creates a new key-value table with the specified name.
 * It allocates all necessary internal structures and prepares the table
 * for use. The returned pointer must be released with destroy_kv_table()
 * when no longer needed.
 *
 * @param name Optional name for the table (can be NULL).
 * @return Pointer to the newly allocated kvTable, or NULL on failure.
 */
static kvTable *alloc_kv_table_base(const char *name, int size, int loadfactor) {
	if (strlen(name) > MAX_NAME_LEN)
		return NULL;

	kvTable *idx = (kvTable *) calloc_mem(1, sizeof(kvTable));
	if (!idx) 
		return NULL;

	strcpy(idx->name, name);
	

	idx->map = (kvNode **) calloc_mem(size, sizeof(kvNode*));
	if (!idx->map) {
		free_mem(idx);
		return NULL;
	}

	idx->mapsize = size;
	idx->lfactor_thrhold = loadfactor;
	idx->elements = 0;
	idx->rehash = 0;

	return idx;
}

kvTable *alloc_kvtable(const char *name) {
	return alloc_kv_table_base(name, DEFAULT_INIT_SIZE, DEFAULT_LOAD_FACTOR);
}

/**
 * @brief Destroys a key-value table and releases all associated resources.
 *
 * This function frees all memory used by the table, including all key-value
 * entries and internal structures. After calling this function, the pointer to
 * the table will be set to NULL to avoid dangling references.
 *
 * @param kvTable Pointer to the kvTable pointer to destroy.
 */
void destroy_kvtable(kvTable **table) {
    kvNode *node;
	if (!table || !*table || !(*table)->map)
        return;

    for (uint32_t i = 0; i < (*table)->mapsize; ++i) {
        while ((*table)->map[i]) {
            node = (*table)->map[i];
            (*table)->map[i] = node->next;
			if (node->entry)
				free_mem(node->entry);
			free_mem(node);
        }
    }

    free_mem((*table)->map);
    (*table)->map = NULL;
    (*table)->elements = 0;
    (*table)->mapsize = 0;
	free_mem(*table);
	*table = NULL;
}

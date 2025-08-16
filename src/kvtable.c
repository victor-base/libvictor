#include <stdint.h>
#include <pthread.h>
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxhash.h"
#include "victorkv.h"
#include "file.h"
#include "panic.h"
#include "mem.h"


#define DEFAULT_LOAD_FACTOR  15
#define DEFAULT_INIT_SIZE   100
#define MAX_NAME_LEN        150
#define MAGIC_HEADER 0x4B565354
/**
 * @brief Header structure stored at the beginning of the dump file.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t  major;
    uint8_t  minor;
    uint8_t  patch;
    uint8_t  _res[5];
    uint32_t elements;
} KVStoreHeader;

typedef struct {
    uint64_t entry_offset;
    uint64_t entry_size;
} KVStoreEntry;

typedef struct {
    uint64_t hash;
    uint32_t klen;
    uint32_t vlen;
    uint8_t  buff[];
} KVEntry;
#pragma pack(pop)


typedef struct KVNode {
    KVEntry *entry;
    struct KVNode *next;
    struct KVNode *prev;
} KVNode;

typedef struct {
    int elements;
    KVStoreEntry *entry_index;
    KVEntry **entries;
} KVIO;

typedef struct KVTable {
    char name[MAX_NAME_LEN];
    pthread_rwlock_t rwlock;
    uint16_t rehash;
    uint16_t lfactor_thrhold;
    uint32_t mapsize;
    uint64_t elements;
    KVNode **map;
} KVTable;

void kv_unsafe_lock(KVTable *table) {
	pthread_rwlock_rdlock(&table->rwlock);
}

void kv_unsafe_unlock(KVTable *table) {
	pthread_rwlock_unlock(&table->rwlock);
}

/**
 * @brief Scans the table for keys that match a given prefix pattern.
 *
 * This function searches through all entries in the hash table and returns
 * those whose keys start with the specified prefix pattern. The scan is
 * performed without acquiring locks (unsafe), so the caller must ensure
 * proper synchronization.
 *
 * @param table Pointer to the KVTable to scan.
 * @param ilike Pointer to the prefix pattern to match against.
 * @param ilen Length of the prefix pattern in bytes.
 * @param results Array of KVResult structures to populate (allocated by caller).
 * @param rlen Maximum number of results that can be stored in the results array.
 *
 * @return Number of results found (>= 0) on success.
 *         KV_ERROR_INVALID_TABLE if table is NULL.
 *         KV_ERROR_INVALID_KEY if ilike is NULL or ilen is invalid.
 *         KV_ERROR_INVALID_VALUE if results is NULL or rlen is invalid.
 *
 * @note This function does not acquire any locks - caller must ensure thread safety.
 * @note The function performs prefix matching, not exact key matching.
 * @note The caller only needs to allocate the array of structures, not individual pointers.
 * @note The returned key and value pointers point to internal memory - do not free.
 */
int kv_unsafe_prefix_scan(KVTable *table, void *ilike, int ilen, KVResult *results, int rlen) {
    int i, r = 0;
    KVNode *node;
    
    if (!table) 
        return KV_ERROR_INVALID_TABLE;
    if (!ilike || ilen <= 0)
        return KV_ERROR_INVALID_KEY;
    if (!results || rlen <= 0)
        return KV_ERROR_INVALID_VALUE;

    for (i = 0; i < (int)table->mapsize && r < rlen; i++) {
        node = table->map[i];
        while (node && r < rlen) {
            if (node->entry && 
                (uint32_t)ilen <= node->entry->klen && 
                !memcmp(ilike, node->entry->buff, ilen)) {
                results[r].key   = node->entry->buff;
                results[r].value = &node->entry->buff[node->entry->klen];
                results[r].klen  = node->entry->klen;
                results[r].vlen  = node->entry->vlen;
                r++;
            }
            node = node->next;
        }
    }
    return r;
}

/**
 * @brief Dumps key-value data from a KVIO structure to a file.
 *
 * This function writes all key-value entries from a KVIO structure to a file in a 
 * structured format. It first writes a header containing metadata about the store,
 * then writes the entry index containing offset and size information for each entry,
 * and finally writes the actual key-value data. The resulting file can be loaded
 * back using kv_store_io_load().
 *
 * @param io Pointer to the KVIO structure containing the data to dump.
 * @param file Pointer to the IOFile to write to.
 *
 * @return 0 on success.
 *         KV_ERROR_FILEIO if any file write operation fails.
 *
 * @note The function writes data in binary format with specific magic numbers
 *       for format identification and validation.
 * @note All entries must be properly initialized in the KVIO structure before calling.
 */
static int kv_store_io_dump(KVIO *io, IOFile *file) {
	PANIC_IF(io == NULL, "invalid io pointer");
	PANIC_IF(file == NULL, "invalid file");
	int i;

	KVStoreHeader header = {
		.magic = MAGIC_HEADER, // 'KVST'
		.major = 1,
		.minor = 0,
		.patch = 0,
		.elements = io->elements
	};

	if (file_write(&header, sizeof(KVStoreHeader), 1, file) != 1)
		return KV_ERROR_FILEIO;

	for (i = 0; i < io->elements; i++)
		if (file_write(&io->entry_index[i], sizeof(KVStoreEntry), 1, file) != 1)
			return KV_ERROR_FILEIO;
	for (i = 0; i < io->elements; i++) {
		if (file_write(io->entries[i], io->entry_index[i].entry_size, 1, file) != 1)
			return KV_ERROR_FILEIO;
	}
	return 0;
}

/**
 * @brief Initializes a KVIO structure for storing key-value entries.
 *
 * This function allocates and initializes all necessary arrays within a KVIO structure
 * to accommodate the specified number of key-value entries. It allocates memory for
 * the entry index array and the entries pointer array, setting up the structure for
 * subsequent loading or dumping operations.
 *
 * @param io Pointer to the KVIO structure to initialize.
 * @param elements Number of key-value entries the structure should accommodate.
 *
 * @return KV_SUCCESS (0) on successful initialization.
 *         KV_ERROR_SYSTEM if memory allocation fails.
 *
 * @note The function zeros out the entire KVIO structure before initialization.
 * @note If allocation fails, any partially allocated memory is automatically freed.
 * @note The elements parameter must be greater than 0.
 */
static int kvio_init(KVIO *io, int elements) {
	PANIC_IF(io == NULL, "invalid io pointer");
	PANIC_IF(elements <= 0, "invalid number of elements");

	memset(io, 0, sizeof(KVIO));
	io->elements = elements;
	io->entry_index = (KVStoreEntry *)calloc_mem(elements, sizeof(KVStoreEntry));
	if (!io->entry_index)
		return KV_ERROR_SYSTEM;
	io->entries = (KVEntry **)calloc_mem(elements, sizeof(KVEntry *));
	if (!io->entries) {
		free_mem(io->entry_index);
		return KV_ERROR_SYSTEM;
	}
	return KV_SUCCESS;
}

/**
 * @brief Releases all memory allocated for a KVIO structure.
 *
 * This function frees all dynamically allocated memory associated with a KVIO structure,
 * including the entry index array and the entries array. It safely handles NULL pointers
 * and can be called multiple times on the same structure without issues.
 *
 * @param io Pointer to the KVIO structure to free (can be NULL).
 *
 * @note This function does not free the individual entries themselves, only the arrays
 *       that hold pointers to them.
 * @note After calling this function, the KVIO structure should be considered invalid.
 */
static void kvio_free(KVIO *io) {
	if (!io)
		return;
	if (io->entries)
		free_mem(io->entries);
	if (io->entry_index)
		free_mem(io->entry_index);
}

/**
 * @brief Loads key-value data from a file into a KVIO structure.
 *
 * This function reads a previously saved key-value store from a file and reconstructs
 * the KVIO structure with all its entries. It first reads and validates the file header,
 * then reads the entry index, and finally loads all the key-value entries into memory.
 * The function performs integrity checks to ensure the file format is valid and the
 * data is consistent.
 *
 * @param io Pointer to the KVIO structure to populate with loaded data.
 * @param file Pointer to the IOFile to read from.
 *
 * @return KV_SUCCESS (0) on successful load.
 *         KV_ERROR_FILEIO if file read operations fail.
 *         KV_ERROR_FILE_INVALID if the file format is invalid or corrupted.
 *         KV_ERROR_SYSTEM if memory allocation fails.
 *
 * @note The function automatically cleans up partially loaded data on failure.
 * @note The file must have been created with kv_store_io_dump() for compatibility.
 */
static int kv_store_io_load(KVIO *io, IOFile *file) {
	PANIC_IF(io == NULL, "invalid io pointer");
	PANIC_IF(file == NULL, "invalid file");
	KVStoreHeader header;
	uint64_t offset;
	int ret, i;

	if (file_read(&header, sizeof(KVStoreHeader), 1, file) != 1)
		return KV_ERROR_FILEIO;

	if (header.magic != MAGIC_HEADER) {
		return KV_ERROR_FILE_INVALID;
	}

	if ((ret = kvio_init(io, header.elements)) != KV_SUCCESS)
		return ret;

	if (file_read(io->entry_index, 
				sizeof(KVStoreEntry), 
				header.elements,file) != header.elements) {
		kvio_free(io);
		return KV_ERROR_FILEIO;
	}

	offset = sizeof(KVStoreHeader) + header.elements * sizeof(KVStoreEntry);
	if (offset != io->entry_index[0].entry_offset || 
	   (uint64_t)offset != (uint64_t)file_tello(file)) {
		kvio_free(io);
		return KV_ERROR_FILE_INVALID;
	}

	for (i = 0; i < (int)header.elements; i++) {
		PANIC_IF(io->entry_index[i].entry_size == 0, "invalid entry size");
		io->entries[i] = (KVEntry *)calloc_mem(1, io->entry_index[i].entry_size);
		if (!io->entries[i]) {
			i = i - 1;
			ret = KV_ERROR_SYSTEM;
			goto error_cleanup;
		}
		if (file_read(io->entries[i], io->entry_index[i].entry_size, 1, file) != 1) {
			ret = KV_ERROR_FILEIO;
			goto error_cleanup;
		}
	}
	return KV_SUCCESS;
error_cleanup:
	for (; i >= 0; i--)
		free_mem(io->entries[i]);
	kvio_free(io);
	return ret;
}

/**
 * @brief Searches for a node in the hash table based on the given key.
 *
 * This function performs a hash-based lookup to find a node that matches the provided key.
 * It computes the hash of the key, determines the appropriate bucket, and then traverses
 * the linked list in that bucket to find a node with matching hash, key length, and key content.
 *
 * @param table Pointer to the hash table (KVTable).
 * @param key Pointer to the key to search for.
 * @param klen Length of the key in bytes.
 *
 * @return Pointer to the matching KVNode if found, NULL otherwise.
 *
 * @note This function does not acquire any locks; the caller is responsible for thread safety.
 * @note The function performs a full key comparison using memcmp for collision resolution.
 */
static KVNode *get_node(KVTable *table, void *key, int klen) {
	if (!table || !key || klen < 0)
		return NULL;
	uint64_t hash = XXH64(key, klen, 0);
	int bucket = hash % table->mapsize;
	KVNode *node = table->map[bucket];
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
 * @param table Pointer to the hash map (KVTable).
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
int kv_get(KVTable *table, void *key, int klen, void **value, int *vlen) {
	if (!table) 
		return KV_ERROR_INVALID_TABLE;
	if (!key || klen <= 0)
		return KV_ERROR_INVALID_KEY;
	if (!value || !vlen)
		return KV_ERROR_INVALID_VALUE;

	pthread_rwlock_rdlock(&table->rwlock);
	KVNode *node = get_node(table, key, klen);
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
 * @param table Pointer to the hash map (KVTable).
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
int kv_get_copy(KVTable *table, void *key, int klen,void **value, int *vlen) {
	if (!table) 
		return KV_ERROR_INVALID_TABLE;
	if (!key || klen <= 0)
		return KV_ERROR_INVALID_KEY;
	if (!value || !vlen)
		return KV_ERROR_INVALID_VALUE;

	pthread_rwlock_rdlock(&table->rwlock);
	KVNode *node = get_node(table, key, klen);
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
 * @param table Pointer to the hash map (KVTable).
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
int kv_del(KVTable *table, void *key, int klen) {
    if (!table)
		return KV_ERROR_INVALID_TABLE;
	if (!key || klen <= 0)
        return KV_ERROR_INVALID_KEY;

	pthread_rwlock_wrlock(&table->rwlock);
    KVNode *node = get_node(table, key, klen);
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
 * @param table Pointer to the hash map structure (`KVTable`).
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
static int rehash(KVTable *table, uint32_t nsize) {
	int bucket;
	PANIC_IF(table == NULL, "invalid table parameter");
	PANIC_IF(nsize == 0 || nsize < table->mapsize, "invalid size parameter");

	KVNode **tmp = (KVNode **) calloc_mem(nsize, sizeof(KVNode*));
	if (!tmp)
		return KV_ERROR_SYSTEM;

	for (uint32_t i = 0; i < table->mapsize; ++i) {
		KVNode *ptr;
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
 * by `KVTable`. If the key already exists, the value is updated in-place (reallocating memory
 * if necessary). If the key does not exist, a new node is created and inserted into the corresponding
 * bucket based on the key's hash.
 *
 * The function also performs rehashing if the load factor exceeds the configured threshold,
 * and ensures thread-safety using a write lock.
 *
 * @param table Pointer to the hash map (KVTable).
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
int kv_put(KVTable *table, void *key, int klen, void *value, int vlen) {
	KVEntry *tmp;
	KVNode *node;
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
			tmp = (KVEntry *) realloc_mem(node->entry, sizeof(KVEntry) + klen + vlen);
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
	tmp = (KVEntry *) calloc_mem(1, sizeof(KVEntry) + klen + vlen);
	if (!tmp) {
		pthread_rwlock_unlock(&table->rwlock);
		return KV_ERROR_SYSTEM;
	}
	tmp->hash = XXH64(key, klen, 0);
	tmp->klen = klen;
	tmp->vlen = vlen;
	memcpy(tmp->buff, key, klen);
	memcpy(&tmp->buff[klen], value, vlen);

	node = (KVNode *) calloc_mem(1, sizeof(KVNode));
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
 * @return Pointer to the newly allocated KVTable, or NULL on failure.
 */
static KVTable *alloc_kv_table_base(const char *name, int size, int loadfactor) {
	if (strlen(name) > MAX_NAME_LEN)
		return NULL;

	KVTable *idx = (KVTable *) calloc_mem(1, sizeof(KVTable));
	if (!idx) 
		return NULL;

	strcpy(idx->name, name);
	
	pthread_rwlock_init(&idx->rwlock, NULL);
	idx->map = (KVNode **) calloc_mem(size, sizeof(KVNode*));
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

KVTable *alloc_kvtable(const char *name) {
	return alloc_kv_table_base(name, DEFAULT_INIT_SIZE, DEFAULT_LOAD_FACTOR);
}

/**
 * @brief Dump the contents of a KVTable to a file.
 *
 * This function serializes the key-value entries stored in a hash table (`KVTable`)
 * and writes them to a binary file in a compact format. It generates a header with
 * versioning information and an index of all entries, followed by the serialized entries.
 *
 * @param table Pointer to the KVTable to be dumped. The table must be write-locked
 *              internally to avoid concurrent modifications during serialization.
 * @param filename Name of the output file to store the serialized table contents.
 *
 * @return KV_SUCCESS on success. Returns a negative error code on failure:
 *         - KV_ERROR_MISMATCH_ELEMENT_COUNT if the counted entries do not match the expected total.
 *         - KV_ERROR_FILEIO if the file could not be opened or written.
 *         - Other errors may be propagated from `kvio_init()` or `kv_store_io_dump()`.
 *
 * @note The function uses a write lock on the table to ensure thread-safety.
 * @note The caller must ensure the file system is writable and that `filename` is valid.
 */
int kv_dump(KVTable *table, const char *filename) {
	uint64_t base_offset;
	KVNode *node = NULL;
	IOFile *file = NULL;
	KVIO io;
	int ret = KV_SUCCESS, i, e;
	
	
	pthread_rwlock_wrlock(&table->rwlock);

	if ((ret = kvio_init(&io, table->elements)) != KV_SUCCESS) {
		pthread_rwlock_unlock(&table->rwlock);
		return ret;
	}
	
	base_offset = sizeof(KVStoreHeader) + table->elements * sizeof(KVStoreEntry);
	e = 0;
	for ( i = 0; i < (int)table->mapsize; i ++ ) {
		node = table->map[i];
		while (node) {
			if (node->entry) {
				io.entry_index[e].entry_size   = sizeof(KVEntry) + node->entry->klen + node->entry->vlen;
				io.entry_index[e].entry_offset = base_offset;
				io.entries[e] = node->entry;
				base_offset += io.entry_index[e].entry_size;
				e++;
			}
			node = node->next;
		}
	}
	if ( e != (int) table->elements ) {
		ret = KV_ERROR_MISMATCH_ELEMENT_COUNT;
		goto cleanup;
	}
	io.elements = e;

	file = file_open(filename, "wb");
	if (!file) {
		ret = KV_ERROR_FILEIO;
		goto cleanup;
	}
	ret = kv_store_io_dump(&io, file);
	file_close(file);
cleanup:
	pthread_rwlock_unlock(&table->rwlock);
	kvio_free(&io);
	return ret;
}


/**
 * @brief Load a key-value table from a binary file.
 *
 * This function reads a serialized key-value store from disk using the `kv_store_io_load` function,
 * reconstructs the internal structure into a `KVTable`, and returns a pointer to the newly created table.
 *
 * The data is read into a temporary `KVIO` structure and then transferred into a new `KVTable` 
 * by allocating and inserting each `KVNode` into the appropriate hash bucket based on the stored hash value.
 *
 * @param filename The path to the binary file containing the serialized key-value store.
 *
 * @return A pointer to the loaded `KVTable` on success, or `NULL` if an error occurs during file reading,
 *         memory allocation, or table construction.
 *
 * @note The returned `KVTable` must be destroyed with `destroy_kvtable()` when no longer needed to avoid memory leaks.
 */
KVTable *load_kvtable(const char *filename) {
	KVTable *table;
	KVNode  *node;
	IOFile  *file;
	KVIO io;
	
	file = file_open(filename, "rb");
	if (!file) 
		return NULL;

	if (kv_store_io_load(&io, file) != KV_SUCCESS) {
		file_close(file);
		return NULL;
	}

	file_close(file);
	table = alloc_kv_table_base("table-loaded", 2 * io.elements, DEFAULT_LOAD_FACTOR);
	if (!table) {
		kvio_free(&io);
		return NULL;
	}

	for ( int i = 0; i < io.elements; i ++ ) {
		int bucket;
		node = (KVNode *) calloc_mem(1, sizeof(KVNode));
		if (!node) {
			destroy_kvtable(&table);
			kvio_free(&io);
			return NULL;
		}
		node->entry = io.entries[i];
		bucket = node->entry->hash % table->mapsize;
		node->next = table->map[bucket];
		node->prev = NULL;
		if (node->next)
			node->next->prev = node;
		table->map[bucket] = node;
		table->elements++;
	}
	kvio_free(&io);
	return table;
}

/**
 * @brief Destroys a key-value table and releases all associated resources.
 *
 * This function frees all memory used by the table, including all key-value
 * entries and internal structures. After calling this function, the pointer to
 * the table will be set to NULL to avoid dangling references.
 *
 * @param KVTable Pointer to the KVTable pointer to destroy.
 */
void destroy_kvtable(KVTable **table) {
    KVNode *node;
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

int kv_size(KVTable *table, uint64_t *sz) {
    if (!table)
        return KV_ERROR_INVALID_TABLE;
    pthread_rwlock_rdlock(&table->rwlock);
    *sz = table->elements;
    pthread_rwlock_unlock(&table->rwlock);
    return KV_SUCCESS;
}
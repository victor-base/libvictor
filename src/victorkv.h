#ifndef _VICTOR_KEY_VALUE
#define _VICTOR_KEY_VALUE 1

#include <stdint.h>


typedef enum {
    KV_SUCCESS,
	KV_KEY_NOT_FOUND,
	KV_ERROR_SYSTEM,
	KV_ERROR_INVALID_TABLE,
	KV_ERROR_INVALID_KEY,
	KV_ERROR_INVALID_VALUE,
	KV_ERROR_FILEIO,
	KV_ERROR_FILE_INVALID,
	KV_ERROR_MISMATCH_ELEMENT_COUNT
} TableErrorCode;

typedef struct KVTable KVTable;

typedef struct {
    void *key;
	void *value;
	int klen;
	int vlen;
} KVResult;

extern const char *table_strerror(TableErrorCode code);

extern void kv_unsafe_lock(KVTable *table);


extern void kv_unsafe_unlock(KVTable *table);

extern int kv_size(KVTable *table, uint64_t *sz);

/**
 * @brief Allocates and initializes a new key-value table (hash map).
 *
 * This function creates a new key-value table with the specified name.
 * It allocates all necessary internal structures and prepares the table
 * for use. The returned pointer must be released with destroy_KVTable()
 * when no longer needed.
 *
 * @param name Optional name for the table (can be NULL).
 * @return Pointer to the newly allocated KVTable, or NULL on failure.
 */
extern KVTable *alloc_kvtable(const char *name);

/**
 * @brief Destroys a key-value table and releases all associated resources.
 *
 * This function frees all memory used by the table, including all key-value
 * entries and internal structures. After calling this function, the pointer to
 * the table will be set to NULL to avoid dangling references.
 *
 * @param KVTable Pointer to the KVTable pointer to destroy.
 */
extern void destroy_kvtable(KVTable **KVTable);

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
extern int kv_put(KVTable *c, void *key, int klen, void *value, int vlen);

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
extern int kv_get(KVTable *c, void *key, int klen, void **value, int *vlen);

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
extern int kv_unsafe_prefix_scan(KVTable *table, void *ilike, int ilen, KVResult *results, int rlen);

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
extern int kv_get_copy(KVTable *table, void *key, int klen,void **value, int *vlen);

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
extern int kv_del(KVTable *table, void *key, int klen);
extern int kv_dump(KVTable *table, const char *filename);
extern KVTable *load_kvtable(const char *filename);
#endif

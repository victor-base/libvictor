#include "index_nsw.h"
#include "store.h"
#include "map.h"

#define NSW_VERSION 1

#pragma pack(push, 1)
/**
 * Represents the serialized header of a Navigable Small World (NSW) graph index.
 *
 * This header is stored at the beginning of the on-disk format and contains
 * all the necessary metadata required to correctly reconstruct the index in memory.
 * All fields are packed to ensure consistent layout across platforms.
 */
typedef struct {
    uint32_t magic;             /**< Magic number used to validate the file format. */
    uint16_t version;           /**< Format version of the dumped graph. */
    uint16_t cmp_type;          /**< Comparison method (e.g., L2NORM, COSINE). */

    uint32_t ef_search;         /**< ef parameter used during graph search. */
    uint32_t ef_construct;      /**< ef parameter used during graph construction. */
    uint32_t odegree_hl;        /**< Hard limit for out-degree (max neighbors). */
    uint32_t odegree_sl;        /**< Soft limit for out-degree (used during insert). */

    uint64_t elements;          /**< Total number of elements stored in the graph. */
    uint16_t dims;              /**< Original number of dimensions for vectors. */
    uint16_t dims_aligned;      /**< Aligned number of dimensions (e.g., padded for SIMD). */
    uint32_t reserved;          /**< Reserved for future use (padding). */

    uint64_t v_offset;          /**< Byte offset in file where vector data starts. */
    uint64_t n_offset;          /**< Byte offset in file where node data starts. */
    uint64_t e_offset;          /**< Offset of the entry node (first search entrypoint). */
} NSWGraphHeader;

/**
 * Represents the serialized form of an individual NSW node.
 *
 * Each node stores the offset to its associated vector, its out-degree,
 * and a fixed-size list of neighbor node offsets. The structure is packed
 * for tight and predictable layout on disk.
 */
typedef struct {
    uint64_t vector;            /**< Offset to the associated vector in the vector section. */
    uint8_t  odegree;           /**< Actual number of outgoing neighbors in use. */
    uint8_t  alive;             /**< Alive flag (non-zero = active, 0 = logically deleted). */
    uint8_t  reserved[6];       /**< Reserved/padding for alignment and future use. */
    uint64_t neighbors[];       /**< Flexible array of neighbor offsets (length = odegree_hl). */
} SNodeNSW;
#pragma pack(pop)


/**
 * Serializes an in-memory NSW node into its on-disk representation.
 *
 * This function transforms an `INodeNSW` structure into a compact `SNodeNSW`
 * format suitable for persistent storage. It translates internal memory pointers
 * (`Vector*` and neighbor `INodeNSW*`) into file offsets using the provided maps.
 * 
 * The output node is padded with zeroed neighbor slots if the current out-degree
 * is less than `max_neighbors`, ensuring fixed-size storage on disk.
 *
 * @param dst - Pointer to the destination SNodeNSW (on-disk format).
 * @param src - Pointer to the source in-memory INodeNSW node.
 * @param max_neighbors - Maximum number of neighbors (allocated slots in `neighbors[]`).
 * @param vat - Map from Vector* (as uintptr_t) to their disk offsets.
 * @param nat - Map from INodeNSW* (as uintptr_t) to their disk offsets.
 */
static void nsw_inode2snode(SNodeNSW *dst, INodeNSW *src, int max_neighbors, Map *vat, Map *nat) {
	int i;
	uint64_t voffset = map_get(vat, (uint64_t)(uintptr_t) src->vector);
	PANIC_IF(voffset == 0, "Invalid vector disk address");
	dst->vector = voffset;
	dst->odegree = (u_int8_t) src->odegree;
	dst->alive   = (u_int8_t) src->alive;
	for (i = 0; i < src->odegree; i++) {
		uint64_t noffset = map_get(nat, (uint64_t)(uintptr_t) src->neighbors[i]);
		PANIC_IF(noffset == 0, "invalid neighbor disk address");
		dst->neighbors[i] = noffset;
	}
	for ( ;i < max_neighbors; i++) {
		dst->neighbors[i] = 0;
	}
}

/**
 * Reconstructs an in-memory NSW node from its serialized (on-disk) representation.
 *
 * This function transforms an `SNodeNSW` structure (read from disk) back into
 * a fully connected `INodeNSW` structure in memory. It uses the `vat` and `nat`
 * maps to resolve disk offsets back to their corresponding memory pointers.
 *
 * The function also increments the `idegree` of each neighbor node referenced,
 * restoring inbound degree metadata used for graph consistency.
 *
 * Neighbor slots beyond the actual out-degree are set to NULL to ensure clean state.
 *
 * @param dst - Pointer to the destination in-memory INodeNSW.
 * @param src - Pointer to the source SNodeNSW loaded from disk.
 * @param max_neighbors - Maximum number of neighbors (allocated slots in `neighbors[]`).
 * @param vat - Map from vector disk offsets (uint64_t) to Vector* (memory pointers).
 * @param nat - Map from node disk offsets (uint64_t) to INodeNSW* (memory pointers).
 */
static void nsw_snode2inode(INodeNSW *dst, SNodeNSW *src, int max_neighbors, Map *vat, Map *nat) {
	int i;
	dst->vector = (Vector *) map_get_p(vat, (uint64_t)(uintptr_t) src->vector);
	PANIC_IF(dst->vector == NULL, "vector can not be null");
	dst->odegree = (int) src->odegree;
	dst->alive   = (int) src->alive;
	for ( i = 0; i < src->odegree; i++) {
		dst->neighbors[i] = (INodeNSW *) map_get_p(nat, src->neighbors[i]);
		PANIC_IF(dst->neighbors[i] == NULL, "neighbor can not be null");
		dst->neighbors[i]->idegree++;
	}
	for ( ;i < max_neighbors; i++) {
		dst->neighbors[i] = NULL;
	}
}

/**
 * Writes the header of an NSW graph index to a binary file.
 *
 * This function serializes the `IndexNSW` configuration and metadata into a
 * compact `NSWGraphHeader` structure and writes it at the beginning of the
 * specified file. This header is essential for correctly reconstructing
 * the index from disk.
 *
 * Fields include:
 * - Comparison type
 * - Graph parameters (`ef`, degrees)
 * - Vector and node section offsets
 * - Entry point offset (`e_offset`)
 *
 * @param fp - File pointer opened in binary write mode (`"wb"` or similar).
 * @param index - Pointer to the in-memory NSW index to serialize.
 * @param v_off - Offset in file where the vector section starts.
 * @param n_off - Offset in file where the node section starts.
 * @param e_off - Offset in file of the graph's entry point node.
 *
 * @return SUCCESS on successful write, SYSTEM_ERROR on I/O failure.
 */
static int nsw_write_header(FILE *fp, const IndexNSW *index, uint64_t v_off, uint64_t n_off, uint64_t e_off) {
    NSWGraphHeader hdr;
    memset(&hdr, 0, sizeof(hdr));

    hdr.magic     = NSW_MAGIC;
    hdr.version   = NSW_VERSION;
    hdr.cmp_type  = index->cmp->type;

    hdr.ef_search     = (uint32_t)index->ef_search;
    hdr.ef_construct  = (uint32_t)index->ef_construct;
    hdr.odegree_hl    = (uint32_t)index->odegree_hl;
    hdr.odegree_sl    = (uint32_t)index->odegree_sl;

    hdr.elements      = index->elements;
    hdr.dims          = index->dims;
    hdr.dims_aligned  = index->dims_aligned;

    hdr.v_offset = v_off;
    hdr.n_offset = n_off;
    hdr.e_offset = e_off;  // obtenido del map inverso: PTR_TO_U64(index->gentry)

    size_t written = fwrite(&hdr, sizeof(hdr), 1, fp);
    return (written == 1) ? SUCCESS : SYSTEM_ERROR;
}

/**
 * Dumps the entire NSW index to a binary file on disk.
 *
 * This function serializes an `IndexNSW` structure into a compact on-disk
 * representation composed of three sections:
 * 1. A header (`NSWGraphHeader`)
 * 2. A vector section (contiguous `Vector` entries)
 * 3. A node section (contiguous `SNodeNSW` entries)
 *
 * The dumping process consists de two passes:
 * - First pass: traverse the graph, write all vectors, and collect pointer-to-offset mappings.
 * - Second pass: serialize each node using the offset maps and write them sequentially.
 *
 * At the end, a compact header is written at the start of the file, containing offsets and metadata
 * required to reconstruct the graph via `nsw_load()`.
 *
 * @param index - Pointer to the in-memory NSW index to serialize.
 * @param filename - Path to the output file to write the dump.
 *
 * @return SUCCESS if the dump was successful, or SYSTEM_ERROR on failure.
 */
int nsw_dump(void *idx, const char *filename) {
	IndexNSW *index = (IndexNSW *) idx;
	SNodeNSW *node;
	INodeNSW *entry;
	FILE *fp;
	uint64_t v_off, n_off, e_off;
	size_t v_sz, n_sz;
	int ret, iv, in;
	Map vat, nat;

	if ((fp = fopen(filename, "wb")) == NULL )
		return SYSTEM_ERROR;

	if (init_map(&vat, index->elements, 10) != MAP_SUCCESS) 
		return SYSTEM_ERROR;

	if (init_map(&nat, index->elements, 10) != MAP_SUCCESS) {
		map_destroy(&vat);
		return SYSTEM_ERROR;
	}

	v_sz = sizeof(Vector) + index->dims_aligned * sizeof(float32_t);
	n_sz = sizeof(SNodeNSW) + index->odegree_hl * sizeof(uint64_t);
	v_off = sizeof(NSWGraphHeader);
	n_off = v_off + v_sz * index->elements;

	iv = in = 0;
	entry = index->lentry;
	// Primero pasada

	fseek(fp, v_off, SEEK_SET);

	while (entry) {
		if (map_insert(&nat, (uintptr_t)entry, n_off + in * n_sz) != MAP_SUCCESS) {
			ret = SYSTEM_ERROR;
			goto cleanup;
		}

		if (map_insert(&vat, (uintptr_t)entry->vector, v_off + iv * v_sz) != MAP_SUCCESS) {
			ret = SYSTEM_ERROR;
			goto cleanup;
		}

		if (fwrite(entry->vector, v_sz, 1, fp) != 1) {
			ret = SYSTEM_ERROR;
			goto cleanup;
		}

		iv++;
		in++;
		entry = entry->next;
	}
	// Segunda pasada
	node = (SNodeNSW *) calloc_mem(1, n_sz);
	if (!node) {
		ret = SYSTEM_ERROR;
		goto cleanup;
	}
	entry = index->lentry;
	
	fseek(fp, n_off, SEEK_SET);
	
	while (entry) {
		nsw_inode2snode(node, entry, index->odegree_hl, &vat, &nat);

		if (fwrite(node, n_sz, 1, fp) != 1) {
			ret = SYSTEM_ERROR;
			goto free_node;
		}
		entry = entry->next;
	}
	
	e_off = map_get(&nat, (uint64_t)(uintptr_t)index->lentry);
	PANIC_IF(e_off == 0, "invalid address");

	fseek(fp, 0, SEEK_SET);
	ret = nsw_write_header(fp, index, v_off, n_off, e_off);

free_node:
	free_mem(node);
cleanup:
	map_destroy(&vat);
	map_destroy(&nat);
	fclose(fp);
	return ret;
}

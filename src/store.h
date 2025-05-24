/*
 * Store - Persistence module for vector and graph data
 *
 * Copyright (C) 2025 Emiliano Alejandro Billi
 *
 * This file is part of the libvictor project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _STORE_H
#define _STORE_H

#include "vector.h"
#include "map.h"

#define MAGIC_SZ size_t(uint32_t)

#define VEC_MAGIC       0x464C5000
/** @brief Magic value for Flat Index. */
#define FLT_MAGIC       0x464C5449  /**< 'FLTI' */
/** @brief Magic value for Hierarchical NSW Index. */
#define HNSW_MAGIC      0x484E5357  /**< 'HNSW' */

#define IO_INIT_VECTORS   (1 << 0) // 0001
#define IO_INIT_MAPS      (1 << 1) // 0010
#define IO_INIT_HEADER    (1 << 2) // 0100
#define IO_INIT_NODES     (1 << 3) // 1000


/**
 * @brief Header structure stored at the beginning of the dump file.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;         /**< File format identifier. */
    uint8_t  major;         /**< Major version. */
    uint8_t  minor;         /**< Minor version. */
    uint8_t  patch;         /**< Patch version. */
    uint8_t  hsize;         /**< Size of header structure in bytes. */
    uint32_t elements;      /**< Number of elements stored. */
    uint16_t method;        /**< Index method used. */
    uint16_t dims;          /**< Original vector dimensions. */
    uint16_t only_vectors;  /**< The file only have vectors  */
    uint16_t dims_aligned;  /**< Aligned vector dimensions. */
    uint16_t vsize;         /**< Size in bytes of a vector. */
    uint16_t nsize;         /**< Size in bytes of a node. */
    uint64_t voff;          /**< Offset to vectors section. */
    uint64_t noff;          /**< Offset to nodes section. */
} StoreHDR;
#pragma pack(pop)

_Static_assert(sizeof(StoreHDR) == 40, "StoreHDR must be exactly 40 bytes");
/**
 * @brief I/O context used for loading or dumping index structures.
 */
typedef struct {
    int      itype;          /**< Index type. */
    uint16_t dims;           /**< Original vector dimensions. */
    uint16_t dims_aligned;   /**< Aligned vector dimensions. */
    uint16_t method;         /**< Indexing method. */
    uint32_t elements;       /**< Number of elements. */

    uint16_t hsize;          /**< Size of header structure. */
    uint16_t nsize;          /**< Size of each node. */
    uint16_t vsize;          /**< Size of each vector. */

    Map vat;                 /**< Map for vectors. */
    Map nat;                 /**< Map for nodes. */

    void   *header;          /**< Pointer to header data. */
    void   **nodes;          /**< Pointer array to nodes. */
    Vector **vectors;        /**< Pointer array to vectors. */
} IOContext;


#define __DEFINE_EXPORT_FN(FUNC_NAME, IndexType, NodeType)               \
static int FUNC_NAME(void *index, IOContext *io) {                       \
    IndexType *idx = index;                                              \
    NodeType   *entry = NULL;                                            \
    if (io_init(io, idx->elements, 0, IO_INIT_VECTORS) != SUCCESS)       \
        return SYSTEM_ERROR;                                             \
    io->nsize = 0;                                                       \
    io->vsize = VECTORSZ(idx->dims_aligned);                             \
    io->dims = idx->dims;                                                \
    io->dims_aligned = idx->dims_aligned;                                \
    io->itype = VEC_MAGIC;                                               \
    io->method = idx->cmp->type;                                         \
    io->hsize  = 0;                                                      \
    entry = idx->head;                                                   \
    for (int i = 0; entry; entry = entry->next, i++) {                   \
        PANIC_IF(i >= (int) io->elements, "index overflow while mapping entries"); \
        io->vectors[i] = entry->vector;                                  \
    }                                                                    \
    return SUCCESS;                                                      \
}



/**
 * @brief Initializes an IOContext structure.
 *
 * @param io Pointer to IOContext.
 * @param elements Number of elements to allocate.
 * @param hdrsz Size of the header.
 * @param mode  Bitmap with the mode
 * @return 0 on success, error code on failure.
 */
extern int io_init(IOContext *io, int elements, int hdrsz, int mode);

/**
 * @brief Frees all vectors stored in the IOContext.
 *
 * @param io Pointer to IOContext.
 */
extern void io_free_vectors(IOContext *io);

/**
 * @brief Frees all memory associated with an IOContext.
 *
 * @param io Pointer to IOContext.
 */
extern void io_free(IOContext *io);

/**
 * @brief Dumps the IOContext to a binary file.
 *
 * @param filename Path to the output file.
 * @param io Pointer to IOContext.
 * @return 0 on success, error code on failure.
 */
extern int store_dump_file(const char *filename, IOContext *io);

/**
 * @brief Loads an IOContext from a binary file.
 *
 * @param filename Path to the input file.
 * @param io Pointer to IOContext.
 * @return 0 on success, error code on failure.
 */
extern int store_load_file(const char *filename, IOContext *io);

#endif /* _STORE_H */
 
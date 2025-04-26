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

/** @brief Magic value for Flat Index. */
#define FLT_MAGIC       0x464C5449  /**< 'FLTI' */
/** @brief Magic value for Flat Index Multi-Processing. */
#define FLT_MP_MAGIC    0x464C544D  /**< 'FLTM' */
/** @brief Magic value for Navigable Small World Index. */
#define NSW_MAGIC       0x4E535747  /**< 'NSWG' */
/** @brief Magic value for Hierarchical NSW Index. */
#define HNSW_MAGIC      0x484E5357  /**< 'HNSW' */

/**
 * @brief Header structure stored at the beginning of the dump file.
 */
typedef struct {
    uint32_t magic;         /**< File format identifier. */
    uint8_t  major;         /**< Major version. */
    uint8_t  minor;         /**< Minor version. */
    uint8_t  patch;         /**< Patch version. */
    uint8_t  hsize;         /**< Size of header structure in bytes. */
    uint32_t elements;      /**< Number of elements stored. */
    uint16_t method;        /**< Index method used. */
    uint16_t dims;          /**< Original vector dimensions. */
    uint16_t dims_aligned;  /**< Aligned vector dimensions. */
    uint16_t vsize;         /**< Size in bytes of a vector. */
    uint16_t nsize;         /**< Size in bytes of a node. */
    uint64_t voff;          /**< Offset to vectors section. */
    uint64_t noff;          /**< Offset to nodes section. */
} StoreHDR;
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

    int maps;                /**< Whether to initialize maps. */
    Map vat;                 /**< Map for vectors. */
    Map nat;                 /**< Map for nodes. */

    void   *header;          /**< Pointer to header data. */
    void   **nodes;          /**< Pointer array to nodes. */
    Vector **vectors;        /**< Pointer array to vectors. */
} IOContext;

/**
 * @brief Initializes an IOContext structure.
 *
 * @param io Pointer to IOContext.
 * @param elements Number of elements to allocate.
 * @param hdrsz Size of the header.
 * @param maps Whether to initialize maps.
 * @return 0 on success, error code on failure.
 */
extern int io_init(IOContext *io, int elements, int hdrsz, int maps);

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
 
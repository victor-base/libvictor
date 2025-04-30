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
#include <errno.h>
#include "store.h"
#include "vector.h"
#include "file.h"

/**
 * @brief Converts an index type to its corresponding magic number.
 *
 * @param itype Index type identifier.
 * @return Corresponding magic number, or 0 if invalid.
 */
static uint32_t index_to_magic(int itype) {
    switch (itype) {
        case FLAT_INDEX:     return FLT_MAGIC;
        case NSW_INDEX:      return NSW_MAGIC;
        case HNSW_INDEX:     return HNSW_MAGIC;
        default:             PANIC_IF(1==1, "invalid index type");
    }
}

/**
 * @brief Converts a magic number to its corresponding index type.
 *
 * @param magic Magic number identifier.
 * @return Corresponding index type, or -1 if unknown.
 */
int magic_to_index(uint32_t magic) {
    switch (magic) {
        case FLT_MAGIC:      return FLAT_INDEX;
        case NSW_MAGIC:      return NSW_INDEX;
        case HNSW_MAGIC:     return HNSW_INDEX;
        default:             return -1;  // desconocido
    }
}

/**
 * @brief Frees all allocated vectors in an IOContext.
 *
 * @param io Pointer to the IOContext containing vectors to free.
 */
void io_free_vectors(IOContext *io) {
    for (int i = 0; i < (int)io->elements; i++) 
        if (io->vectors[i])
            free_vector(&io->vectors[i]);
}


/**
 * @brief Frees all memory associated with an IOContext structure.
 *
 * Releases the header, vectors, nodes, and maps if present.
 * Resets the IOContext to zero after freeing.
 *
 * @param io Pointer to the IOContext to free.
 */
void io_free(IOContext *io) {
    PANIC_IF(io == NULL, "invalid load context");
    if (io->header)  free_mem(io->header);
    if (io->vectors) free_mem(io->vectors);
    if (io->nodes) {
        int elements = io->elements;
        for (int i = 0; i < elements; i++) {
            if (io->nodes[i]) {
                free_mem(io->nodes[i]);
                io->nodes[i] = NULL;
            }
        }  
        free_mem(io->nodes);
    }

    map_destroy(&io->vat);
    map_destroy(&io->nat);

    memset(io, 0, sizeof(IOContext));
}

/**
 * @brief Initializes an IOContext structure.
 *
 * Allocates memory for header, vectors, and nodes arrays.
 * Optionally initializes vector and node maps if requested.
 *
 * @param io Pointer to the IOContext to initialize.
 * @param elements Number of elements to allocate.
 * @param hdrsz Size of the header in bytes.
 * @param maps Non-zero to initialize maps, zero to skip.
 * @return 0 on success, or an error code on failure.
 */
int io_init(IOContext *io, int elements, int hdrsz, int mode) {
    PANIC_IF(io == NULL, "invalid load context");
    PANIC_IF(hdrsz == 0 && (mode & IO_INIT_HEADER)  , "invalid header size");

    io->header  = NULL;
    io->nodes   = NULL;
    io->vectors = NULL;
    io->elements = elements;
    io->itype = -1;
    io->hsize = hdrsz;
    io->vsize = io->nsize = 0;
    io->nat = MAP_INIT();
    io->vat = MAP_INIT();

    if (mode & IO_INIT_HEADER)
        if ((io->header = calloc_mem(1, hdrsz)) == NULL)
            goto error_return;
            
    if (mode & IO_INIT_VECTORS)
        if ((io->vectors = calloc_mem(elements, sizeof(Vector *))) == NULL) 
            goto error_return;
            
    if (mode & IO_INIT_NODES)
        if ((io->nodes = calloc_mem(elements, sizeof(void *))) == NULL)
            goto error_return;


    if (mode & (IO_INIT_VECTORS | IO_INIT_MAPS))
        if (init_map(&io->vat, elements/10, 15) == MAP_ERROR_ALLOC)
            goto error_return;

    if (mode & (IO_INIT_NODES | IO_INIT_MAPS))
        if (init_map(&io->nat, elements/10, 15) == MAP_ERROR_ALLOC) 
            goto error_return;

    return SUCCESS;
error_return:
    io_free(io);
    return SYSTEM_ERROR;
}


/**
 * @brief Dumps an IOContext to a binary file.
 *
 * Writes the header, vectors, and nodes sequentially to the file.
 * Updates offsets for vectors and nodes in the StoreHDR structure.
 * Validates the IOContext before dumping.
 *
 * @param filename Path to the output file.
 * @param io Pointer to the IOContext to dump.
 * @return 0 on success, or an error code on failure.
 */
int store_dump_file(const char *filename, IOContext *io) {
    IOFile *fp = NULL;
    StoreHDR hdr;
    uint64_t voff;
    uint64_t noff;
    int ret = SUCCESS;


    memset(&hdr, 0, sizeof(StoreHDR));

    PANIC_IF(filename == NULL, "invalid filename pointer");
    PANIC_IF(io == NULL, "invalid io context");
    PANIC_IF(io->vectors == NULL, "vectors could not be null");

    PANIC_IF(index_to_magic(io->itype) == 0, "invalid index type");

    if ((fp = file_open(filename, "wb")) == NULL)
        return FILEIO_ERROR;

    if (file_seek(fp, sizeof(StoreHDR), SEEK_SET) != 0) {
        ret = FILEIO_ERROR;
        goto end;
    }

    if (io->hsize > 0)
        if (file_write(io->header, io->hsize, 1, fp) != 1) {
            ret = FILEIO_ERROR;
            goto end;
        }

    voff = (uint64_t)file_tello(fp);
    for (int i = 0; i < (int)io->elements; i++) {
        if (file_write(io->vectors[i], io->vsize, 1, fp) != 1) {
            ret = FILEIO_ERROR;
            goto end;
        }
    }

    if (io->nodes != NULL) {
        noff = (uint64_t) file_tello(fp);
        for (int i = 0; i < (int) io->elements; i++) {
            if (file_write(io->nodes[i], io->nsize, 1, fp) != 1) {
                ret = FILEIO_ERROR;
                goto end;
            }
        }
        hdr.only_vectors = 0;
    } else {
        hdr.only_vectors = 1;
        noff = 0;
    }
    hdr.magic = index_to_magic(io->itype);
    hdr.hsize = io->hsize;
    hdr.nsize = io->nsize;
    hdr.vsize = io->vsize;
    hdr.noff = noff;
    hdr.voff = voff;
    hdr.elements = io->elements;
    hdr.method = io->method;
    hdr.dims = io->dims;
    hdr.dims_aligned = io->dims_aligned;
    
    if (file_seek(fp, 0, SEEK_SET) != 0) {
        ret = FILEIO_ERROR;
        goto end;
    }

    if (file_write(&hdr, sizeof(StoreHDR), 1, fp) != 1)
        ret = FILEIO_ERROR;

end:
    file_close(fp);
    return ret;
}

/**
 * @brief Loads an index and its associated vectors and nodes from a binary file.
 *
 * Parses the file header, initializes the IOContext, and loads vectors and nodes
 * into memory. Validates file structure and internal offsets during the load.
 *
 * @param filename Path to the binary file to load.
 * @param io Pointer to an IOContext structure to initialize and populate.
 * @return 0 on success, or an error code on failure.
 */
int store_load_file(const char *filename, IOContext *io) {
    IOFile *fp = NULL;
    off_t pos;
    StoreHDR hdr;
    int ret = SUCCESS;
    int mode = 0;
    int itype;

    memset(&hdr, 0, sizeof(StoreHDR));


    if ((fp = file_open(filename, "rb")) == NULL)
        return FILEIO_ERROR;

    if (file_read(&hdr, sizeof(StoreHDR), 1, fp) != 1) {
        file_close(fp);
        return FILEIO_ERROR;
    }

    if ((itype = magic_to_index(hdr.magic)) == -1) {
        file_close(fp);
        return INVALID_FILE;
    }

    if (hdr.hsize != 0)
        mode = IO_INIT_HEADER;

    if (hdr.only_vectors)
        mode |= IO_INIT_VECTORS;
    else 
        mode |= IO_INIT_NODES | IO_INIT_VECTORS;

    if (io_init(io, hdr.elements, hdr.hsize, mode) != SUCCESS) {
        file_close(fp);
        return SYSTEM_ERROR;
    }

    io->dims         = hdr.dims;
    io->dims_aligned = hdr.dims_aligned;
    io->method       = hdr.method;
    io->elements     = hdr.elements;
    io->itype        = itype;

    if (mode & IO_INIT_HEADER) {
        if (file_read(io->header, hdr.hsize, 1, fp) != 1) {
            file_close(fp);
            return FILEIO_ERROR;
        }
    }

    if ((pos = file_tello(fp)) != (off_t)-1 && (uint64_t) pos != hdr.voff) {
        ret = INVALID_FILE;
        goto error_return;
    }

    for (int i = 0; i < (int) hdr.elements; i++ ) {
        io->vectors[i] = alloc_vector(hdr.dims_aligned);
        if (io->vectors[i] == NULL) {
            ret = SYSTEM_ERROR;
            goto error_return;
        }
        if (file_read(io->vectors[i], hdr.vsize, 1, fp) != 1) {
            ret = FILEIO_ERROR;
            goto error_return;
        }
    }

    if (mode & IO_INIT_NODES) {
        if ((pos = file_tello(fp)) != (off_t)-1 && (uint64_t) pos != hdr.noff) {
            ret = INVALID_FILE;
            goto error_return;
        }

        for (int i = 0; i < (int) hdr.elements; i++ ) {
            io->nodes[i] = calloc_mem(1, hdr.nsize);
            if (io->nodes[i] == NULL) {
                ret = SYSTEM_ERROR;
                goto error_return;
            }
            if (file_read(io->nodes[i], hdr.nsize, 1, fp) != 1) {
                ret = FILEIO_ERROR;
                goto error_return;
            }
        }
    }

    file_close(fp);
    return SUCCESS;

error_return:
    if (fp != NULL) file_close(fp);
    io_free_vectors(io);
    io_free(io);
    return ret;
}


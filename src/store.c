#include "store.h"
#include "vector.h"

/* Convierte el tipo de índice a su correspondiente MAGIC */
static uint32_t index_to_magic(int method) {
    switch (method) {
        case FLAT_INDEX:     return FLT_MAGIC;
        case FLAT_INDEX_MP:  return FLT_MP_MAGIC;
        case NSW_INDEX:      return NSW_MAGIC;
        case HNSW_INDEX:     return HNSW_MAGIC;
        default:             return 0;  // inválido
    }
}

/* Convierte un MAGIC a su método (índice) correspondiente */
static int magic_to_index(uint32_t magic) {
    switch (magic) {
        case FLT_MAGIC:      return FLAT_INDEX;
        case FLT_MP_MAGIC:   return FLAT_INDEX_MP;
        case NSW_MAGIC:      return NSW_INDEX;
        case HNSW_MAGIC:     return HNSW_INDEX;
        default:             return -1;  // desconocido
    }
}

void io_free_vectors(IOContext *io) {
	for (int i = 0; i < (int)io->elements; i++) 
		if (io->vectors[i])
			free_vector(&io->vectors[i]);
}

int io_init(IOContext *io, int elements, int hdrsz, int maps) {
	PANIC_IF(io == NULL, "invalid load context");

	io->header  = NULL;
	io->nodes   = NULL;
	io->vectors = NULL;
	io->elements = elements;
	io->itype = -1;
	io->hsize = io->vsize = io->nsize = 0;

	io->header = calloc_mem(1, hdrsz);
	if (!io->header) return SYSTEM_ERROR;
	if ((io->vectors = calloc_mem(elements, sizeof(Vector *))) == NULL) {
		free_mem(io->header);
		return SYSTEM_ERROR;
	}

	if ((io->nodes = calloc_mem(elements, sizeof(void *))) == NULL) {
		free_mem(io->header);
		free_mem(io->vectors);
		return SYSTEM_ERROR;
	}

	if (maps) {
		if (init_map(&io->vat, 10000, 15) == MAP_ERROR_ALLOC) {
			free_mem(io->header);
			free_mem(io->vectors);
			return SYSTEM_ERROR;
		}

		if (init_map(&io->nat, 10000, 15) == MAP_ERROR_ALLOC) {
			map_destroy(&io->vat);
			free_mem(io->header);
			free_mem(io->vectors);
			return SYSTEM_ERROR;
		}
		//io->maps;
	}

	return SUCCESS;
}

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
	if (io->maps) {
		map_destroy(&io->vat);
		map_destroy(&io->vat);
	}
	memset(io, 0, sizeof(IOContext));
}


int store_dump_file(const char *filename, IOContext *io) {
	FILE *fp = NULL;
	StoreHDR hdr;
	uint64_t voff;
	uint64_t noff;
	int ret = SUCCESS;

	PANIC_IF(filename == NULL, "invalid filename pointer");
	PANIC_IF(io == NULL, "invalid io context");
	PANIC_IF(io->header == NULL, "header could not be null");
	PANIC_IF(io->nodes == NULL, "nodes could not be null");
	PANIC_IF(io->vectors == NULL, "vectors could not be null");

	PANIC_IF(index_to_magic(io->itype) == 0, "invalid index type");

	if ((fp = fopen(filename, "wb")) == NULL)
		return FILEIO_ERROR;

	if (fseek(fp, sizeof(StoreHDR), SEEK_SET) != 0) {
		ret = FILEIO_ERROR;
		goto end;
	}

	if (fwrite(io->header, io->hsize, 1, fp) != 1) {
		ret = FILEIO_ERROR;
		goto end;
	}

	voff = ftell(fp);
	
	for (int i = 0; i < (int)io->elements; i++) {
		if (fwrite(io->vectors[i], io->vsize, 1, fp) != 1) {
			ret = FILEIO_ERROR;
			goto end;
		}
	}

	noff = ftell(fp);
	for (int i = 0; i < (int) io->elements; i++) {
		if (fwrite(io->nodes[i], io->nsize, 1, fp) != 1) {
			ret = FILEIO_ERROR;
			goto end;
		}
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
	
	if (fseek(fp, 0, SEEK_SET) != 0) {
		ret = FILEIO_ERROR;
		goto end;
	}

	if (fwrite(&hdr, sizeof(StoreHDR), 1, fp) != 1)
		ret = FILEIO_ERROR;

end:
	fclose(fp);
	return ret;
}

int store_load_file(const char *filename, IOContext *io) {
	FILE *fp = NULL;
	off_t pos;
	StoreHDR hdr;
	int ret = SUCCESS;

	memset(&hdr, 0, sizeof(StoreHDR));


	if ((fp = fopen(filename, "rb")) == NULL)
		return FILEIO_ERROR;

	if (fread(&hdr, sizeof(StoreHDR), 1, fp) != 1) {
		fclose(fp);
		return FILEIO_ERROR;
	}

	if ((io->itype = magic_to_index(hdr.magic)) == -1) {
		fclose(fp);
		return INVALID_FILE;
	}

	io->dims         = hdr.dims;
	io->dims_aligned = hdr.dims_aligned;
	io->method       = hdr.method;
	io->elements     = hdr.elements;

	if (io_init(io, hdr.elements, hdr.hsize, 0) != SUCCESS) {
		fclose(fp);
		return SYSTEM_ERROR;
	}

	if (fread(io->header, hdr.hsize, 1, fp) != 1) {
		fclose(fp);
		return FILEIO_ERROR;
	}

	if ((pos = ftello(fp)) != (off_t)-1 && (uint64_t) pos != hdr.voff) {
		ret = INVALID_FILE;
		goto error_return;
	}

	for (int i = 0; i < (int) hdr.elements; i++ ) {
		io->vectors[i] = alloc_vector(hdr.dims_aligned);
		if (io->vectors[i] == NULL) {
			ret = SYSTEM_ERROR;
			goto error_return;
		}
		if (fread(io->vectors[i], hdr.vsize, 1, fp) != 1) {
			ret = FILEIO_ERROR;
			goto error_return;
		}
	}

	if ((pos = ftello(fp)) != (off_t)-1 && (uint64_t) pos != hdr.noff) {
		ret = INVALID_FILE;
		goto error_return;
	}

	for (int i = 0; i < (int) hdr.elements; i++ ) {
		io->nodes[i] = calloc_mem(1, hdr.nsize);
		if (io->nodes[i] == NULL) {
			ret = SYSTEM_ERROR;
			goto error_return;
		}
		if (fread(io->nodes[i], hdr.nsize, 1, fp) != 1) {
			ret = FILEIO_ERROR;
			goto error_return;
		}
	}

	fclose(fp);
	return SUCCESS;

error_return:
	if (fp != NULL) fclose(fp);
	io_free_vectors(io);
	io_free(io);
	return ret;
}


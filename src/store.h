#ifndef _STORE_H
#define _STORE_H

#define MAGIC_SZ size_t(uint32_t)

#define FLT_MAGIC       0x464C5449  // 'FLTI' - Flat Index
#define FLT_MP_MAGIC    0x464C544D  // 'FLTM' - Flat Index MP
#define NSW_MAGIC       0x4E535747  // 'NSWG' - Navigable Small World
#define HNSW_MAGIC      0x484E5357  // 'HNSW' - Hierarchical NSW

#include "vector.h"
#include "map.h"

typedef struct {
	uint32_t magic;
	uint8_t  major;
	uint8_t  minor;
	uint8_t  patch;
	uint8_t  hsize;
	uint32_t elements;
	uint16_t method;
	uint16_t dims;
	uint16_t dims_aligned;
	uint16_t vsize;
	uint16_t nsize;
	uint64_t voff;
	uint64_t noff;
} StoreHDR;


typedef struct {
	int      itype;
	uint16_t dims;
	uint16_t dims_aligned;
	uint16_t method;
	uint32_t elements;

	uint16_t hsize;
	uint16_t nsize;
	uint16_t vsize;

	int maps;
	Map vat;
	Map nat;

	void   *header;
	void   **nodes;
	Vector **vectors;
} IOContext;



extern int io_init(IOContext *io, int elements, int hdrsz, int maps);

extern void io_free_vectors(IOContext *io);

extern void io_free(IOContext *io);


extern int store_dump_file(const char *filename, IOContext *io);


extern int store_load_file(const char *filename, IOContext *io);


#endif 
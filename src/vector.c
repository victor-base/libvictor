/*
 * vector.c - Implementation of Vector Structure and Memory Management
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Contact: emiliano.billi@gmail.com
 *
 * Purpose:
 * This file implements the memory allocation and deallocation functions 
 * for the `Vector` structure used in the vector cache database.
 */
#include <string.h>
#include "config.h"
#include "vector.h"
#include "mem.h"


Vector *alloc_vector(uint16_t dims_aligned) {
	return (Vector *) aligned_calloc_mem(16, VECTORSZ(dims_aligned));
}
 
/**
 * Allocates and initializes a new `Vector` structure.
 *
 * @param id    Unique identifier for the vector.
 * @param src   Pointer to the source data (can be NULL).
 * @param dims  Number of dimensions (size of the vector).
 * @return Pointer to the newly allocated `Vector` structure, or NULL on failure.
 */
Vector *make_vector(uint64_t id, float32_t *src, uint16_t dims) {
    Vector *vector;
    uint16_t dims_aligned = ALIGN_DIMS(dims);
    

	vector = alloc_vector(dims_aligned);
    if (vector && src) {
        memcpy(vector->vector, src, dims * sizeof(float32_t));
        vector->id = id;
        return vector;
    }
    return NULL;
}

/**
 * Frees the memory allocated for a `Vector` structure.
 *
 * @param vector Pointer to the `Vector` structure to be freed.
 */
void free_vector(Vector **vector) {
    if (vector && *vector) {
		free_aligned_mem(*vector);
		*vector = NULL;
    }
}

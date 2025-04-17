/*
 * heap.h - Heap implementation used for vector indexing (Flat, HNSW, etc.)
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
 * Contact: emiliano.billi@gmail.com
 */
#ifndef _HEAP_H
#define _HEAP_H 1
#include "victor.h"
#include "mem.h"

#define HEAP_NODE_INT(__u__)   ((__u__).value.i)
#define HEAP_NODE_PTR(__u__)   ((__u__).value.p)
#define HEAP_NODE_U64(__u__)   ((__u__).value.u)
#define HEAP_NODE_S64(__u__)   ((__u__).value.s)

#define LCHD(i) (2 * (i) + 1)
#define RCHD(i) (2 * (i) + 2)
#define PARENT(i)  ( (i) == 0 ? 0 : ((i) - 1) / 2 )

#define NOLIMIT_HEAP -1
#define DEFAULT_SIZE 50

#define HEAP_MIN 1
#define HEAP_MAX 2


typedef struct {

	union {
		uint64_t u;
		int64_t  s;
		int      i;
		void    *p;
	} value;

    float32_t distance;
} HeapNode;

typedef struct {
    int (*is_better_match) (float32_t, float32_t);
    
    HeapNode *heap;
    int c_size;    /* Current size */
    int m_size;    /* Max size     */
    int type;	   /* Heap Type    */
    int e;         /* Number of elements in the heap */
} Heap;

typedef enum {
    HEAP_SUCCESS = 0,             // Operación exitosa
    HEAP_ERROR_NULL = -1,         // Puntero nulo en heap o parámetros
    HEAP_ERROR_EMPTY = -2,        // Heap vacío
    HEAP_ERROR_FULL = -3,         // Heap lleno y no puede crecer
    HEAP_ERROR_ALLOC = -4,        // Error de memoria (calloc/realloc)
    HEAP_ERROR_INVALID_TYPE = -5, // Tipo de heap desconocido o inválido
    HEAP_ERROR_INSERT = -6,       // Error genérico al insertar
    HEAP_ERROR_RESIZE = -7,       // Fallo al redimensionar el heap
    HEAP_ERROR_UNSUPPORTED = -8   // Operación no soportada
} HeapErrorCode;

/* === Initialization === */

/**
 * Initializes a new heap structure.
 *
 * @param h Pointer to the heap to initialize.
 * @param type HEAP_MIN or HEAP_MAX, determines the comparison behavior.
 * @param max_size Maximum number of elements allowed (use NOLIMIT_HEAP for unbounded).
 * @param cmp Pointer to a comparison function: returns true if the first argument is a better match.
 * @return HEAP_SUCCESS on success, or an error code (e.g., HEAP_ERROR_ALLOC).
 */
extern int init_heap(Heap *h, int type, int max_size, int (*cmp)(float32_t, float32_t));


/* === Heap state / status === */

/**
 * Returns the number of elements currently in the heap.
 *
 * @param h Pointer to the heap.
 * @return Heap size.
 */
extern int heap_size(Heap *h);

/**
 * Checks whether the heap is full.
 *
 * @param h Pointer to the heap.
 * @return 0 if not full, 1 if full.
 */
extern int heap_full(Heap *h);


/* === Heap operations === */

/**
 * Inserts a new node into the heap.
 *
 * @param h Pointer to the heap.
 * @param node Pointer to the node to insert.
 * @return HEAP_SUCCESS on success, or an error code (e.g., HEAP_ERROR_FULL).
 */
extern int heap_insert(Heap *h, const HeapNode *node);

/**
 * Replaces the top (best or worst) element of the heap with a new node,
 * and restores the heap invariant.
 *
 * @param h Pointer to the heap.
 * @param node Pointer to the new node.
 * @return HEAP_SUCCESS on success, or an error code.
 */
extern int heap_replace(Heap *h, const HeapNode *node);

/**
 * Retrieves and removes the top node from the heap.
 *
 * @param h Pointer to the heap.
 * @param node Pointer to store the removed node.
 * @return HEAP_SUCCESS on success, or an error code (e.g., HEAP_ERROR_EMPTY).
 */
extern int heap_pop(Heap *h, HeapNode *node);

/**
 * Retrieves (but does not remove) the top node from the heap.
 *
 * @param h Pointer to the heap.
 * @param node Pointer to store the peeked node.
 * @return HEAP_SUCCESS on success, or an error code (e.g., HEAP_ERROR_EMPTY).
 */
extern int heap_peek(Heap *h, HeapNode *node);


extern void heap_destroy(Heap *h);

#endif
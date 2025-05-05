/**
 * @file asort.c
 * @brief Asynchronous Top-K Sort (ASort) implementation using a min-heap.
 *
 * This module provides a lightweight mechanism for asynchronously collecting
 * and maintaining the best k matches from a stream of inputs.
 * It operates in single-threaded mode and accumulates inputs until manually closed.
 *
 * @copyright
 * Copyright (C) 2025 Emiliano Alejandro Billi
 *
 * This file is part of libvictor (or your project name if you prefer).
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 */
#include "method.h"
#include "heap.h"
#include "mem.h"

typedef struct ASort {
	Heap *heap;
} ASort;

int init_asort(ASort *as, int n, int method) {
	CmpMethod *cmp;

	if (as == NULL) 
		return INVALID_ARGUMENT;
	
	cmp = get_method(method);
	if (!cmp) 
		return INVALID_METHOD;

	as->heap = calloc_mem(1, sizeof(Heap));
	if (!as->heap) 
		return SYSTEM_ERROR;

	if (init_heap(as->heap, HEAP_WORST_TOP, n, cmp->is_better_match) != HEAP_SUCCESS) {
		free_mem(as->heap);
		as->heap = NULL;
		return SYSTEM_ERROR;
	}
	return SUCCESS;
}

/**
 * @brief Adds multiple match results into the ASort structure.
 *
 * Inserts match results into the heap, keeping only the best k elements.
 * If the heap is full, it replaces the worst element if a better match is found.
 *
 * @param[in,out] as Pointer to the ASort context.
 * @param[in] inputs Array of match results to insert.
 * @param[in] n Number of match results in the input array.
 * @return SUCCESS on success, or an error code on failure.
 */
int as_update(ASort *as, MatchResult *inputs, int n) {
	int i;
	if (as == NULL || inputs == NULL || n <= 0  || as->heap == NULL)
		return INVALID_ARGUMENT;
	
	for (i = 0; i < n; i++) {
		HeapNode node;
		if (heap_full(as->heap)) {
			if (heap_peek(as->heap, &node) != HEAP_SUCCESS)
				return SYSTEM_ERROR;

			if (as->heap->is_better_match(inputs[i].distance, node.distance)) {
				node.distance = inputs[i].distance;
				HEAP_NODE_U64(node) = inputs[i].id;
				if (heap_replace(as->heap, &node) != HEAP_SUCCESS)
					return SYSTEM_ERROR;
			}
		} else{
			node.distance = inputs[i].distance;
			HEAP_NODE_U64(node) = inputs[i].id;
			if (heap_insert(as->heap, &node) == HEAP_ERROR_FULL)
				return SYSTEM_ERROR;
		}
	}
	return SUCCESS;
}

/**
 * @brief Finalizes the ASort context and extracts sorted results.
 *
 * Pops elements from the internal heap into the output array in approximate order.
 * If the output array is NULL, simply releases internal resources.
 *
 * @param[in,out] as Pointer to the ASort context.
 * @param[out] outputs Array to store the extracted match results, or NULL to just free resources.
 * @param[in] n Maximum number of results to extract.
 * @return Number of results extracted on success, 0 if only freed, or an error code on failure.
 */
int as_close(ASort *as, MatchResult *outputs, int n) {
	int k;
	if (as == NULL || as->heap == NULL || n < 0)
		return -1;
	
	if (outputs == NULL) {
		heap_destroy(as->heap);
		as->heap = NULL;
		return 0;
	}

	k = n > as->heap->e ? as->heap->e : n;
	for (int i = k -1; i >= 0; i--) {
		HeapNode node;
		if (heap_pop(as->heap, &node) == HEAP_ERROR_EMPTY)
			return -1;
		outputs[i].distance = node.distance;
		outputs[i].id = HEAP_NODE_U64(node);
	}
	heap_destroy(as->heap);
    as->heap = NULL;
    return k;
}
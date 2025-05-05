/*
 * heap.c - Heap implementation used for vector indexing (Flat, HNSW, etc.)
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
 *
 * Contact: emiliano.billi@gmail.com
 */
#include "panic.h"
#include "heap.h"

/**
 * Doubles the internal capacity of the heap if it is marked as NOLIMIT_HEAP.
 *
 * @param h Pointer to the heap.
 * @return HEAP_SUCCESS if resized, HEAP_ERROR_ALLOC on failure, or HEAP_ERROR_RESIZE if resizing is not allowed.
 */
static int resize_heap(Heap *h) {
    HeapNode *tmp;
    size_t   hsz;
    PANIC_IF(h == NULL, "h is NULL");
    PANIC_IF(h->heap == NULL, "h->heap is NULL");

    if (h->m_size == NOLIMIT_HEAP) {
        hsz = 2 * h->c_size;
        tmp = (HeapNode *) realloc_mem(h->heap, hsz * sizeof(HeapNode));
        if (tmp == NULL) 
            return HEAP_ERROR_ALLOC;
        h->c_size = hsz;
        h->heap = tmp;
        return HEAP_SUCCESS;
    }
    return HEAP_ERROR_RESIZE;
}

/**
 * Swaps two HeapNode elements.
 */
static void swap(HeapNode *a, HeapNode *b) {
    HeapNode temp = *a;
    *a = *b;
    *b = temp;
}

/**
 * Restores the heap invariant by moving the root element down.
 *
 * @param h Pointer to the heap.
 * @return HEAP_SUCCESS.
 */
static int heapify_down(Heap *h) {
    int i = 0;

    PANIC_IF(h == NULL, "h is NULL");
    PANIC_IF(h->heap == NULL, "h->heap is NULL");

    if (h->e == 0)
        return HEAP_SUCCESS;

    while (1) {
        int l = LCHD(i);
        int r = RCHD(i);
        int target = i;

        if (h->type == HEAP_BETTER_TOP) {
            if (l < h->e && h->is_better_match(h->heap[l].distance, h->heap[target].distance)) {
                target = l;
            }

            if (r < h->e && h->is_better_match(h->heap[r].distance, h->heap[target].distance)) {
                target = r;
            }
        } else {
            if (l < h->e && !h->is_better_match(h->heap[l].distance, h->heap[target].distance)) {
                target = l;
            }

            if (r < h->e && !h->is_better_match(h->heap[r].distance, h->heap[target].distance)) {
                target = r;
            }
        }
        if (target == i) break; 

        swap(&h->heap[i], &h->heap[target]);
        i = target;
    }

    return HEAP_SUCCESS;
}

/**
 * Restores the heap invariant by moving the last inserted element up.
 *
 * @param h Pointer to the heap.
 * @return HEAP_SUCCESS.
 */
static int heapify_up(Heap *h) {
    int i;
    
    PANIC_IF(h == NULL, "h is NULL");
    PANIC_IF(h->heap == NULL, "h->heap is NULL");

    if (h->e == 0)
        return HEAP_SUCCESS;

    i = h->e-1;

    while(1) {
        if ( i == 0 ) break;
        int p = PARENT(i);

        if (h->type == HEAP_BETTER_TOP) {
            if (h->is_better_match(h->heap[i].distance, h->heap[p].distance)) {
                swap(&h->heap[i], &h->heap[p]);
                i = p;
            } else {
                break;
            }
        } else {
            if (!h->is_better_match(h->heap[i].distance, h->heap[p].distance)) {
                swap(&h->heap[i], &h->heap[p]);
                i = p;
            } else {
                break;
            }
        }
    }
    return HEAP_SUCCESS;
}

/**
 * Retrieves (but does not remove) the top node from the heap.
 *
 * @param h Pointer to the heap.
 * @param node Pointer to store the peeked node.
 * @return HEAP_SUCCESS on success, or an error code (e.g., HEAP_ERROR_EMPTY).
 */
int heap_peek(Heap *h, HeapNode *node) {
    PANIC_IF(h == NULL, "h is NULL");
    PANIC_IF(h->heap == NULL, "h->heap is NULL");
    PANIC_IF(node == NULL, "node is null");

    if (h->e == 0)
        return HEAP_ERROR_EMPTY;

    *node = h->heap[0];
    return HEAP_SUCCESS;
}

/**
 * Replaces the top (best or worst) element of the heap with a new node,
 * and restores the heap invariant.
 *
 * @param h Pointer to the heap.
 * @param node Pointer to the new node.
 * @return HEAP_SUCCESS on success, or an error code.
 */
int heap_replace(Heap *h, const HeapNode *node) {
    PANIC_IF(h == NULL, "h is NULL");
    PANIC_IF(h->heap == NULL, "h->heap is NULL");
    PANIC_IF(node == NULL, "node is null");

    if (h->e == 0)
        return HEAP_ERROR_EMPTY;

    h->heap[0] = *node;
    if (h->e > 1)
        return heapify_down(h);
    return HEAP_SUCCESS;
}

/**
 * Retrieves and removes the top node from the heap.
 *
 * @param h Pointer to the heap.
 * @param node Pointer to store the removed node.
 * @return HEAP_SUCCESS on success, or an error code (e.g., HEAP_ERROR_EMPTY).
 */
int heap_pop(Heap *h, HeapNode *node) {
    PANIC_IF(h == NULL, "h is NULL");
    PANIC_IF(h->heap == NULL, "h->heap is NULL");

    if (h->e == 0)
        return HEAP_ERROR_EMPTY;

    if (node) 
        *node = h->heap[0];
    h->heap[0] = h->heap[--(h->e)];
    return h->e == 0 ? HEAP_SUCCESS : heapify_down(h);
}

/**
 * Inserts a new node into the heap.
 *
 * @param h Pointer to the heap.
 * @param node Pointer to the node to insert.
 * @return HEAP_SUCCESS on success, or an error code (e.g., HEAP_ERROR_FULL).
 */
int heap_insert(Heap *h, const HeapNode *node) {
    PANIC_IF(h == NULL, "h is NULL");
    PANIC_IF(h->heap == NULL, "h->heap is NULL");
    PANIC_IF(node == NULL, "node is null");


    if (h->e == h->c_size) {
        if (h->c_size == h->m_size)
            return HEAP_ERROR_FULL;
        int r = resize_heap(h);
        if (r != HEAP_SUCCESS)
            return r;
    }

    h->heap[h->e++] = *node;
    if (h->e > 1)
        return heapify_up(h);
    return HEAP_SUCCESS;
}


/**
 * Initializes a new heap structure.
 *
 * @param h Pointer to the heap to initialize.
 * @param type HEAP_MIN or HEAP_MAX, determines the comparison behavior.
 * @param max_size Maximum number of elements allowed (use NOLIMIT_HEAP for unbounded).
 * @param cmp Pointer to a comparison function: returns true if the first argument is a better match.
 * @return HEAP_SUCCESS on success, or an error code (e.g., HEAP_ERROR_ALLOC).
 */
int init_heap(Heap *h, int type, int max_size, int (*cmp)(float32_t, float32_t)) {
    PANIC_IF(h == NULL, "h is NULL");
    PANIC_IF(cmp == NULL, "comparator cmp is NULL");

    if (type != HEAP_BETTER_TOP && type != HEAP_WORST_TOP)
        return HEAP_ERROR_INVALID_TYPE;

    if (max_size < 0 && max_size != NOLIMIT_HEAP)
        return HEAP_ERROR_UNSUPPORTED;

    h->e = 0;
    h->m_size = max_size;
    h->c_size = (h->m_size == NOLIMIT_HEAP) ? DEFAULT_SIZE : h->m_size;
    h->type = type;
    h->heap = (HeapNode *) calloc_mem(h->c_size, sizeof(HeapNode));
    if (h->heap == NULL) {
        return HEAP_ERROR_ALLOC;
    }
    h->is_better_match = cmp;
    return HEAP_SUCCESS;
}

int heap_cap(Heap *h) {
    return h->m_size;
}

void heap_destroy(Heap *h) {
    if ( h && h->heap) {
        free_mem(h->heap);
        h->heap = NULL;
        h->e = 0;
        h->c_size = 0;
        h->m_size = 0;
        h->type = 0;
        h->is_better_match = NULL;
    }
}

/**
 * Returns the number of elements currently in the heap.
 *
 * @param h Pointer to the heap.
 * @return Heap size.
 */

int heap_size(Heap *h) {
    PANIC_IF(h == NULL, "h is NULL");
    PANIC_IF(h->heap == NULL, "h->heap is NULL");
    return h->e;
}

/**
 * Checks whether the heap is full.
 *
 * @param h Pointer to the heap.
 * @return 0 if not full, 1 if full
 */
int heap_full(Heap *h) {
    PANIC_IF(h == NULL, "h is NULL");
    PANIC_IF(h->heap == NULL, "h->heap is NULL");

    if (h->m_size == NOLIMIT_HEAP || h->e < h->m_size) 
        return 0;

    return 1;
}
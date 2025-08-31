#include "config.h"
#include "iflat_utils.h"
#include "mem.h"
#include "vector.h"
#include "method.h"
#include "heap.h"
#include "panic.h"

/*
 * insert_node - Inserts a new node at the head of the list.
 *
 * @param head - Pointer to the head of the linked list.
 * @param node - Node to insert.
 */
void insert_node(INodeFlat **head, INodeFlat *node) {
    node->next = *head;
    node->prev = NULL;

    if (*head) 
        (*head)->prev = node;

    *head = node;
}

/*
 * search_node - Searches for a vector by its ID.
 *
 * @param head - Pointer to the head of the linked list.
 * @param id   - Unique identifier of the vector.
 *
 * @return Pointer to the node if found, NULL otherwise.
 */
INodeFlat *search_node(INodeFlat **head, uint64_t id) {
    if (!head || !(*head)) return NULL;

    INodeFlat *current = *head;
    while (current) {
        if (current->vector && current->vector->id == id) 
            return current;
        current = current->next;
    }
    return NULL;
}

/*
 * delete_node - Deletes a node by its vector ID.
 *
 * @param head - Pointer to the head of the linked list.
 * @param id   - Unique identifier of the vector.
 *
 * @return SUCCESS if deletion was successful, INVALID_ID if not found.
 */
int delete_node(INodeFlat **head, INodeFlat *node) {
    PANIC_IF(head == NULL || *head == NULL || node == NULL, "null pointer in delete_node");

    if (node->prev)
        node->prev->next = node->next;
    else
        *head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    free_mem(node->vector);
    free_mem(node);
    return SUCCESS;
}


/*
 * flat_linear_search - Finds the top-N closest matches in a flat index with optional tag filtering.
 *
 * Performs a linear search over a linked list of indexed vectors, identifying the top-N closest
 * matches to a given query vector. Uses a heap to efficiently keep the best results and supports
 * filtering by tag (bitmask).
 *
 * @param current      Pointer to the head of the linked list of INodeFlat.
 * @param tag          Bitmask filter: only vectors whose tag shares at least one bit will be considered.
 *                     If tag == 0, no tag filtering is applied.
 * @param v            Pointer to the query vector.
 * @param dims_aligned Number of aligned dimensions in the vector.
 * @param result       Output array of MatchResult to store the best matches.
 * @param n            Number of top matches to return.
 * @param cmp          Pointer to the CmpMethod structure for distance comparison.
 * @return SUCCESS if the search was successful, SYSTEM_ERROR on memory error.
 */
int flat_linear_search(INodeFlat *current, uint64_t tag, float32_t *restrict v, uint16_t dims_aligned, MatchResult *result, int n, CmpMethod *cmp) {
    Heap heap = HEAP_INIT();
    HeapNode node;

    if (init_heap(&heap, HEAP_WORST_TOP, n, cmp->is_better_match) == HEAP_ERROR_ALLOC)
        return SYSTEM_ERROR;
    
    int i, k;
    for (i = 0; i < n; i++) {
        result[i].distance = cmp->worst_match_value;
        result[i].id = NULL_ID;
    }
    while (current) {
		if (!tag || (tag & current->vector->tag )) {
			node.distance = cmp->compare_vectors(current->vector->vector, v, dims_aligned);
			HEAP_NODE_PTR(node) = current;
			PANIC_IF(heap_insert_or_replace_if_better(&heap, &node) != HEAP_SUCCESS, "error in heap");
		}
		current = current->next;
    }

    k = heap_size(&heap);
	while (k > 0) {
		heap_pop(&heap, &node);
		result[--k].distance = node.distance;
		result[k].id = ((INodeFlat *)HEAP_NODE_PTR(node))->vector->id;
	}
    heap_destroy(&heap);
    return SUCCESS;
}


INodeFlat *make_inodeflat(uint64_t id, uint64_t tag, float32_t *vector, uint16_t dims) {	
    INodeFlat *node = (INodeFlat *) calloc_mem(1, sizeof(INodeFlat));
    
    if (node) {
        if ((node->vector = make_vector(id, tag, vector, dims)) == NULL) {
            free_mem(node);
            node = NULL;
        }
    }
    return node;
}
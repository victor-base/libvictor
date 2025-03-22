#include "config.h"
#include "iflat_utils.h"
#include "mem.h"
#include "vector.h"
#include "method.h"


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
int delete_node(INodeFlat **head, uint64_t id) {
    if (!head || !(*head)) return INVALID_ID;

    INodeFlat *current = *head;
    while (current) {
        if (current->vector->id == id) {
            if (current->prev) {
                current->prev->next = current->next;  
            } else {
                *head = current->next; 
            }
            if (current->next) {
                current->next->prev = current->prev;
            }
            free_mem(current->vector);
            free_mem(current);          
            return SUCCESS;
        }
        current = current->next;
    }
    return INVALID_ID;
}

/*
 * shift_right_mr - Shifts elements to the right in a MatchResult array.
 *
 * This function shifts elements in a MatchResult array one position to the right.
 * It is used when inserting a new match into a sorted list of nearest neighbors,
 * ensuring that the new element is inserted in the correct position while
 * preserving the order of the previous elements.
 *
 * Steps:
 * 1. Start from the last element in the array and move each element one position to the right.
 * 2. Continue shifting elements until reaching the beginning of the array.
 *
 * @param result - Pointer to the MatchResult array.
 * @param len    - Number of elements to shift.
 */
static void shift_right_mr(MatchResult *result, int len) {
    int i;
    for (i = len-1; i > 0; i--)
        result[i] = result[i-1];
    return;
}

/*
 * flat_linear_search - Performs a linear search for the best match in a flat index.
 *
 * This function iterates through a linked list of indexed vectors, comparing each one
 * with the query vector to determine the closest match based on the specified 
 * comparison method. The function updates the MatchResult structure with the best match found.
 *
 * Steps:
 * 1. Initialize the result with the worst possible match value.
 * 2. Iterate through each node in the linked list.
 * 3. Compute the distance between the query vector and the current node's vector.
 * 4. If the computed distance is better than the current best match, update the result.
 * 5. Continue until all elements have been checked.
 *
 * @param current      - Pointer to the head of the linked list of INodeFlat.
 * @param v            - Pointer to the query vector.
 * @param dims_aligned - Number of aligned dimensions in the vector.
 * @param result       - Pointer to the MatchResult structure to store the best match.
 * @param cmp          - Pointer to the CmpMethod structure that defines the comparison functions.
 */
void flat_linear_search(INodeFlat *current, float32_t *v, uint16_t dims_aligned, MatchResult *result, CmpMethod *cmp) {
    float32_t distance;
    result->distance = cmp->worst_match_value;
    result->id = 0;

    while (current) {
        distance = cmp->compare_vectors(current->vector->vector, v, dims_aligned);
        if (cmp->is_better_match(distance, result->distance)) {
            result->id = current->vector->id;
            result->distance = distance;
        }
        current = current->next;
    }
}


/*
 * flat_linear_search_n - Finds the top-N closest matches in a flat index.
 *
 * This function performs a linear search over a linked list of indexed vectors,
 * identifying the top-N closest matches to a given query vector. The results
 * are stored in a sorted array, ensuring that the closest matches appear first.
 *
 * Steps:
 * 1. Initialize the top-N results with the worst possible match values.
 * 2. Iterate through the linked list of vectors.
 * 3. Compute the distance between the query vector and each node's vector.
 * 4. If the computed distance is among the top-N best matches:
 *    a. Shift elements to the right to maintain sorted order.
 *    b. Insert the new match in the correct position.
 * 5. Continue until all elements have been checked.
 *
 * @param current      - Pointer to the head of the linked list of INodeFlat.
 * @param v            - Pointer to the query vector.
 * @param dims_aligned - Number of aligned dimensions in the vector.
 * @param result       - Pointer to an array of MatchResult structures to store the top-N matches.
 * @param n            - Number of top matches to find.
 * @param cmp          - Pointer to the CmpMethod structure that defines the comparison functions.
 */
void flat_linear_search_n(INodeFlat *current, float32_t *v, uint16_t dims_aligned, MatchResult *result, int n, CmpMethod *cmp) {
    float32_t distance;
    int i, k;
    for (i = 0; i < n; i++) {
        result[i].distance = cmp->worst_match_value;
        result[i].id = 0;
    }
    while (current) {
        distance = cmp->compare_vectors(current->vector->vector, v, dims_aligned);
        for (k = 0; k < n; k++) {
            if (cmp->is_better_match(distance, result[k].distance)) {
                shift_right_mr(&result[k], n - k - 1);
                result[k].distance = distance;
                result[k].id = current->vector->id;
                break;
            }
        }
        current = current->next;
    }
}
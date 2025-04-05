#ifndef _INDEX_FLAT_UTILS_H
#define _INDEX_FLAT_UTILS_H

#include "vector.h"
#include "method.h"

/*
* INodeFlat - Structure for linked list nodes in the flat index.
*
* Each node contains a vector and pointers to its neighbors.
*/
typedef struct node_flat {
    Vector *vector;          // Pointer to the stored vector

    struct node_flat *next;  // Pointer to the next node
    struct node_flat *prev;  // Pointer to the previous node
} INodeFlat;


/*
 * insert_node - Inserts a new node at the head of the list.
 *
 * @param head - Pointer to the head of the linked list.
 * @param node - Node to insert.
 */
extern void insert_node(INodeFlat **head, INodeFlat *node);

/*
 * search_node - Searches for a vector by its ID.
 *
 * @param head - Pointer to the head of the linked list.
 * @param id   - Unique identifier of the vector.
 *
 * @return Pointer to the node if found, NULL otherwise.
 */
extern INodeFlat *search_node(INodeFlat **head, uint64_t id);

/*
 * delete_node - Deletes a node by its vector ID.
 *
 * @param head - Pointer to the head of the linked list.
 * @param id   - Unique identifier of the vector.
 *
 * @return SUCCESS if deletion was successful, INVALID_ID if not found.
 */
extern int delete_node(INodeFlat **head, INodeFlat *node);


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
extern void flat_linear_search(INodeFlat *current, float32_t *v, uint16_t dims_aligned, MatchResult *result, CmpMethod *cmp);


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
extern int flat_linear_search_n(INodeFlat *current, float32_t *v, uint16_t dims_aligned, MatchResult *result, int n, CmpMethod *cmp);

#endif
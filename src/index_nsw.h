#ifndef _NSW_H
#define _NSW_H 1

#include "index.h"
#include "victor.h"
/**
 * Initializes a generic `Index` structure using the NSW (Navigable Small World)
 * implementation as its backend.
 *
 * This function configures the function pointers and internal state of the
 * `Index` abstraction to point to the NSW-specific methods and data structures.
 * It delegates the internal memory and configuration setup to `nsw_init()`.
 *
 * On failure to initialize the internal NSW index, the outer `Index` structure
 * is cleaned up to avoid memory leaks.
 *
 * Parameters:
 *  - idx: Pointer to the high-level `Index` structure to initialize.
 *  - method: Integer identifier for the vector comparison method (e.g., L2, cosine).
 *  - dims: Dimensionality of the vectors the index will store.
 *  - context: Optional configuration parameters for NSW behavior (can be NULL).
 *
 * Returns:
 *  - SUCCESS if the index is successfully initialized.
 *  - SYSTEM_ERROR if memory allocation or NSW setup fails.
 */
extern int nsw_index(Index *idx, int method, uint16_t dims, NSWContext *context);

#endif
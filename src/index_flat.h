/*
 * index_flat.h - Flat Index Implementation for Vector Cache Database
 * 
 * Copyright (C) 2025 Emiliano A. Billi
 *
 * This header defines the interface for a flat index structure used in 
 * vector-based search and retrieval. The flat index stores vectors in a 
 * simple linked list, making it suitable for small datasets or baseline 
 * implementations before transitioning to more complex structures like HNSW.
 *
 * Features:
 * - Linear search over a doubly linked list of stored vectors.
 * - Supports L2 and Cosine similarity metrics.
 * - Thread-safe with read-write locks.
 * - Simple and efficient for small to medium datasets.
 *
 * Usage:
 * - `flat_index()` initializes a flat index instance with a given distance metric.
 * - The flat index integrates into the generic `Index` interface, allowing 
 *   seamless swapping of index types.
 *
 * Considerations:
 * - For large-scale datasets, hierarchical or tree-based structures should be 
 *   considered for better search efficiency.
 *
 * License:
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Contact: emiliano.billi@gmail.com
 */

 #ifndef __FLAT_INDEX_H
 #define __FLAT_INDEX_H 1
 
#include "index.h"
 /**
  * Initializes a flat index structure.
  *
  * This function sets up a flat index with the specified distance metric.
  * The index is managed through a doubly linked list, making it easy to 
  * implement but not optimal for very large datasets.
  *
  * @param idx    - Pointer to the generic Index structure.
  * @param method - Distance metric method (e.g., L2NORM, COSINE).
  * @param dims   - Number of dimensions of stored vectors.
  *
  * @return SUCCESS on success, SYSTEM_ERROR on failure.
  */
 extern int flat_index(Index *idx, int method, uint16_t dims);
 
 extern int flat_index_load(Index *idx, IOContext *io);
 #endif
 
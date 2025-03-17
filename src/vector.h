/*
 * vector.h - Vector Structure and Memory Management
 * 
 * Copyright (C) 2025 Emiliano A. Billi
 *
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
 *
 * Purpose:
 * This header defines the `Vector` structure used for storing and managing
 * floating-point vectors in the vector cache database. It also provides 
 * memory management functions to create and free vectors efficiently.
 */

 #ifndef __VECTOR_H
 #define __VECTOR_H 1
 
 #include "victor.h"
 
 #define ALIGN_DIMS(d) (((d) + 3) & ~3)

 /**
  * Structure representing a vector with an identifier and a dynamically
  * sized floating-point array.
  */
 typedef struct {
     uint64_t id;       // Unique identifier for the vector
     float32_t vector[]; // Flexible array member for storing float values
 } Vector;
 
 /**
  * Allocates and initializes a new `Vector` structure.
  *
  * @param id    Unique identifier for the vector.
  * @param src   Pointer to the source data (can be NULL).
  * @param dims  Number of dimensions (size of the vector).
  * @return Pointer to the newly allocated `Vector` structure, or NULL on failure.
  */
 extern Vector *make_vector(uint64_t id, float32_t *src, uint16_t dims);
 
 /**
  * Frees the memory allocated for a `Vector` structure.
  *
  * @param vector Pointer to the `Vector` structure to be freed.
  */
 extern void free_vector(Vector **vector);
 
 #endif // __VECTOR_H
 
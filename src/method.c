/*
 * method.c - Vector Cache Database Implementation
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
 * Contact: emiliano.billi@gmail.com
 *
 * Purpose:
 * This file implements vector comparison methods used in the vector cache database.
 * It provides different distance metrics such as L2 Norm and Cosine Similarity,
 * which are used to compare vectors efficiently.
 */

#include "config.h"
#include "method.h"
#include "math.h"
 
 /**
  * Array of comparison methods available in the system.
  * Each method contains:
  *  - `worst_match_value`: The worst possible value for this metric (e.g., infinity for L2 Norm).
  *  - `is_better_match`: A function pointer that determines if a new match is better than the previous.
  *  - `compare_vectors`: A function pointer to the actual comparison function.
  */
 static CmpMethod __methods[] = {
     { // L2NORM: Euclidean Distance
        .type = 0,
        .worst_match_value = INFINITY,              // Worst match is infinite distance
        .is_better_match = euclidean_distance_best, // Function to determine best match
        .compare_vectors = euclidean_distance,      // Function to compute L2 norm distance
     },
     { // COSINE: Cosine Similarity
        .type = 1,
        .worst_match_value = -1.0,                  // Worst match is -1 (opposite vectors)
        .is_better_match = cosine_similarity_best,  // Function to determine best match
        .compare_vectors = cosine_similarity,       // Function to compute cosine similarity
     },
      { // DOTP: Dot Product
        .type = 2,
        .worst_match_value = -1.0,                  // Worst match is -1 (opposite vectors)
        .is_better_match = cosine_similarity_best,  // Function to determine best match
        .compare_vectors = dot_product,       // Function to compute cosine similarity
    },
 };
 
/**
 * Retrieves the comparison method based on the provided method ID.
 * 
 * @param method The method ID (e.g., L2NORM, COSINE)
 * @return A pointer to the corresponding CmpMethod structure, or NULL if invalid.
 */
CmpMethod *get_method(int method) {
unsigned long m = (unsigned long) method;
    if (method < 0 || m >= NUM_METHODS)
        return NULL;
    return &__methods[m];
}
 

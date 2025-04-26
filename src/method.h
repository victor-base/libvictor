/*
 * method.h - Vector Cache Database Implementation
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
 *
 * Purpose:
 * This header file defines the structure and interface for vector comparison methods.
 * It provides function pointers for different similarity/distance metrics and a method
 * to retrieve the desired comparison function.
 */

#ifndef __METHOD_H
#define __METHOD_H 1

#include "vector.h"
 
/**
 * Structure defining a vector comparison method.
 * 
 * Fields:
 *  - `worst_match_value`: The worst possible similarity score for this method.
 *  - `is_better_match`: Function pointer that determines if a new match is better.
 *  - `compare_vectors`: Function pointer for computing the similarity/distance.
 */
typedef struct {
    int       type;
    float32_t worst_match_value;  // Worst possible score for this metric
    int       (*is_better_match) (float32_t, float32_t);  // Function to compare match quality
    float32_t (*compare_vectors) (float32_t *, float32_t *, int);  // Function to compute similarity/distance
} CmpMethod;
 

/**
 * Macro to determine the number of available methods.
 */
#define NUM_METHODS (sizeof(__methods) / sizeof(CmpMethod))

/**
 * Retrieves the requested comparison method.
 * 
 * @param method The method ID (e.g., L2NORM, COSINE)
 * @return A pointer to the corresponding CmpMethod, or NULL if the method is invalid.
 */
extern CmpMethod *get_method(int method);

#endif  // __METHOD_H

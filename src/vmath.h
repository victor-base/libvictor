/*
 * math.h - Vector Cache Database Implementation
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
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Contact: emiliano.billi@gmail.com
 */

#ifndef _VICTOR_MATH_
#define _VICTOR_MATH_
#include <math.h>
#include "vector.h"

float32_t euclidean_distance(float32_t *v1, float32_t *v2, int dims);
float32_t euclidean_distance_squared(float32_t *v1, float32_t *v2, int dims);
float32_t cosine_similarity(float32_t *v1, float32_t *v2, int dims);
float32_t dot_product(float32_t *v1, float32_t *v2, int dims);
int euclidean_distance_best(float32_t a, float32_t b);
int cosine_similarity_best(float32_t a, float32_t b);

float32_t norm(float32_t *v, int dims);
void normalize(float32_t *v, int dims);
#endif 
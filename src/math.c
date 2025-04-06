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

#include <math.h>
#include <stdint.h>
#include "config.h"

#ifdef __ARM_NEON
#include <arm_neon.h>

#endif

// SSE architecture macro declaration.
#ifdef __SSE__
#include <xmmintrin.h>

#endif

#include "vector.h"
/**
 * @brief Computes the Euclidean distance (L2 norm) between two vectors.
 *
 * This function calculates the squared root of the sum of squared differences
 * between corresponding elements of two floating-point vectors.
 *
 * @param v1 Pointer to the first vector (array of float32_t).
 * @param v2 Pointer to the second vector (array of float32_t).
 * @param dims The number of dimensions (size) of the vectors.
 * @return The Euclidean distance between the two vectors.
 *
 * @note Both vectors must have the same dimensionality (`dims`).
 * @note This function assumes that the input vectors are aligned and
 *       that `dims` is a multiple of 4 for optimal SIMD performance.
 */
float32_t euclidean_distance(float32_t *v1, float32_t *v2, int dims)
{
    float32_t sum = 0.0f;
    int i;

#ifdef __ARM_NEON // NEON SIMD implementation.
    float32x4_t acc = vdupq_n_f32(0.0f);
    for (i = 0; i < dims; i += 4)
    {
        float32x4_t a = vld1q_f32(v1 + i);
        float32x4_t b = vld1q_f32(v2 + i);
        float32x4_t diff = vsubq_f32(a, b);
        acc = vmlaq_f32(acc, diff, diff);
    }
    sum = vaddvq_f32(acc);

#elif defined(__SSE__) // SSE SIMD implementation.

    __m128 acc = _mm_setzero_ps();
    for (i = 0; i < dims; i += 4)
    {
        __m128 a = _mm_load_ps(v1 + i);
        __m128 b = _mm_load_ps(v2 + i);

        // substraction
        __m128 diff = _mm_sub_ps(a, b);

        // Accumulates the squared differences of two vectors
        acc = _mm_add_ps(acc, _mm_mul_ps(diff, diff));
    }

    // Perform horizontal addition manually
    __m128 temp = _mm_add_ps(acc, _mm_movehl_ps(acc, acc));
    __m128 hadd = _mm_add_ss(temp, _mm_shuffle_ps(temp, temp, 1));

    // convert to a normal float
    sum = _mm_cvtss_f32(hadd);

#else // Fallback

    for (i = 0; i < dims; i++)
    {
        float32_t diff = v1[i] - v2[i];
        sum += diff * diff;
    }

#endif

    return sqrtf(sum);
}

int euclidean_distance_best(float32_t a, float32_t b)
{
    return (a < b) ? 1 : 0;
}

/**
 * @brief Computes the cosine similarity between two vectors.
 *
 * Cosine similarity measures the cosine of the angle between two vectors
 * in a multi-dimensional space. It is commonly used to determine the
 * similarity between embeddings or feature vectors.
 *
 *
 * @param v1 Pointer to the first vector (array of float32_t).
 * @param v2 Pointer to the second vector (array of float32_t).
 * @param dims The number of dimensions (size) of the vectors.
 * @return The cosine similarity value, ranging from -1 to 1.
 *
 * @note A value of `1.0` indicates identical direction, `0.0` means the vectors
 *       are orthogonal (no similarity), and `-1.0` indicates opposite directions.
 * @note Both vectors must have the same dimensionality (`dims`).
 * @note This function assumes that the input vectors are aligned and that `dims`
 *       is a multiple of 4 for optimal SIMD performance.
 * @note If either vector has a zero magnitude, the function returns `0.0`
 *       to avoid division by zero.
 */
float32_t cosine_similarity(float32_t *v1, float32_t *v2, int dims)
{
    float32_t dot = 0.0f, norm1 = 0.0f, norm2 = 0.0f;
    int i;

#ifdef __ARM_NEON
    float32x4_t acc_dot = vdupq_n_f32(0.0f);
    float32x4_t acc_norm1 = vdupq_n_f32(0.0f);
    float32x4_t acc_norm2 = vdupq_n_f32(0.0f);

    for (i = 0; i < dims; i += 4)
    {
        float32x4_t a = vld1q_f32(v1 + i);
        float32x4_t b = vld1q_f32(v2 + i);

        acc_dot = vmlaq_f32(acc_dot, a, b);
        acc_norm1 = vmlaq_f32(acc_norm1, a, a); // ||v1||^2
        acc_norm2 = vmlaq_f32(acc_norm2, b, b); // ||v2||^2
    }

    dot = vaddvq_f32(acc_dot);
    norm1 = sqrtf(vaddvq_f32(acc_norm1));
    norm2 = sqrtf(vaddvq_f32(acc_norm2));

#elif defined(__SSE__)
    __m128 acc_dot = _mm_setzero_ps();

    __m128 acc_norm1 = _mm_setzero_ps();
    __m128 acc_norm2 = _mm_setzero_ps();

    for (i = 0; i < dims; i += 4)
    {
        __m128 a = _mm_load_ps(v1 + i);
        __m128 b = _mm_load_ps(v2 + i);

        acc_dot = _mm_add_ps(acc_dot, _mm_mul_ps(a, b)); // Dot product

        acc_norm1 = _mm_add_ps(acc_norm1, _mm_mul_ps(a, a)); // Normalization
        acc_norm2 = _mm_add_ps(acc_norm2, _mm_mul_ps(b, b));
    }

    __m128 temp = _mm_add_ps(acc_dot, _mm_movehl_ps(acc_dot, acc_dot));
    dot = _mm_cvtss_f32(_mm_add_ss(temp, _mm_shuffle_ps(temp, temp, 1)));

    temp = _mm_add_ps(acc_norm1, _mm_movehl_ps(acc_norm1,acc_norm1));
    norm1 = sqrtf(_mm_cvtss_f32(_mm_add_ss(temp, _mm_shuffle_ps(temp, temp, 1))));
   
    temp = _mm_add_ps(acc_norm2, _mm_movehl_ps(acc_norm2, acc_norm2));
    norm2 = sqrtf(_mm_cvtss_f32(_mm_add_ss(temp, _mm_shuffle_ps(temp, temp, 1))));


#else //Fallback case
    for (i = 0; i < dims; i++)
    {
        dot += v1[i] * v2[i];
        norm1 += v1[i] * v1[i];
        norm2 += v2[i] * v2[i];
    }
    norm1 = sqrtf(norm1);
    norm2 = sqrtf(norm2);
#endif

    if (norm1 == 0.0f || norm2 == 0.0f)
        return 0.0f;

    return dot / (norm1 * norm2);
}

int cosine_similarity_best(float32_t a, float32_t b)
{
    return (a > b) ? 1 : 0;
}
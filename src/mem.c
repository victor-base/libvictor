/*
 * mem.c - Vector Cache Database Memory Manager
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
 * This file implements a memory manager for the vector cache database.
 * It provides an abstraction over memory allocation and deallocation,
 * enabling better control and optimization of memory usage.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
 /**
  * Allocates memory for an array of `__count` elements of `__size` bytes each.
  * This function abstracts `calloc` to allow for future optimizations.
  */
void *calloc_mem(size_t __count, size_t __size) {
    return calloc(__count, __size);
}
 
/**
  * Reallocates memory pointed to by `__ptr` to a new size `__size`.
  * This function abstracts `realloc` to allow for future optimizations or custom allocators.
  */
void *realloc_mem(void *__ptr, size_t __size) {
    return realloc(__ptr, __size);
}


/**
  * Frees allocated memory.
  * This function abstracts `free` to allow for future memory management strategies.
  */
void free_mem(void *__mem) {
    free(__mem);
}


 /**
 * Allocates zero-initialized, aligned memory.
 *
 * This function returns memory aligned to `alignment` bytes,
 * and ensures the entire block is zeroed like `calloc`.
 *
 * On POSIX systems, uses `posix_memalign`.
 * On Windows, uses `_aligned_malloc` + `memset`.
 *
 * @param alignment Byte alignment (must be power of two).
 * @param size      Total number of bytes to allocate.
 * @return Pointer to zero-initialized aligned memory, or NULL on failure.
 */
void *aligned_calloc_mem(size_t alignment, size_t size) {
    void *ptr = NULL;

#if defined(_WIN32)
    ptr = _aligned_malloc(size, alignment);
    if (ptr) memset(ptr, 0, size);
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    if (posix_memalign(&ptr, alignment, size) == 0)
        memset(ptr, 0, size);
    else
        ptr = NULL;
#else
    #error "aligned_calloc_mem is not supported on this platform"
#endif

    return ptr;
}

/**
 * Frees memory allocated by aligned_calloc_mem.
 * Uses the appropriate deallocator per platform.
 *
 * @param ptr Pointer to memory previously allocated with aligned_calloc_mem.
 */
void free_aligned_mem(void *ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}
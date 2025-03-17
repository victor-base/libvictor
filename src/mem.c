/*
 * mem.c - Vector Cache Database Memory Manager
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
 * This file implements a memory manager for the vector cache database.
 * It provides an abstraction over memory allocation and deallocation,
 * enabling better control and optimization of memory usage.
 */

 #include <stdio.h>
 #include <stdlib.h>
 
 /**
  * Allocates memory for an array of `__count` elements of `__size` bytes each.
  * This function abstracts `calloc` to allow for future optimizations.
  */
 void *calloc_mem(size_t __count, size_t __size) {
     return calloc(__count, __size);
 }
 
 /**
  * Frees allocated memory.
  * This function abstracts `free` to allow for future memory management strategies.
  */
 void free_mem(void *__mem) {
     free(__mem);
 }
 
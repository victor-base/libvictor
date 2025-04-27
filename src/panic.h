/*
 * Internal error handling philosophy for libvictor
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
 * libvictor is a public library and exposes well-defined return codes
 * for all user-facing functions.
 *
 * However, any internal inconsistency (e.g., invalid heap pointers,
 * broken invariants, corrupted memory) is treated as a fatal bug
 * in the library itself.
 *
 * In such cases, the system is no longer trustworthy and we prefer
 * to panic early and clearly rather than risk silent corruption.
 *
 * This is intentional and by design.
 */

#ifndef _PANIC_H
#define _PANIC_H 1

#include <stdio.h>
#include <stdlib.h>
 
/* Always-on panic macro */
#define PANIC_IF(cond, msg)                         \
    do {                                            \
        if (cond) {                                 \
            fprintf(stderr,                         \
                "[CORE PANIC] %s:%d: %s\n",         \
                __FILE__, __LINE__, msg);           \
            abort();                                \
        }                                           \
    } while (0)


#endif
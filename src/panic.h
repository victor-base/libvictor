/*
 * Internal error handling philosophy for libvictor
 *
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
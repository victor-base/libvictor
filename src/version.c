/*
* version.c - Version Information for libvictor
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
* Contact: emiliano.billi@gmail.com
*/

#include <stdio.h>
#include <stdlib.h>
#include "version.h"

/**
 * Returns the version string of the library with build info.
 */
const char* __LIB_VERSION(void) {
    static char version_info[256];
    snprintf(version_info, sizeof(version_info), 
             "libvictor version %s - Built on %s for %s/%s",
             __LIB_VERSION_STRING, __BUILD_DATE, __BUILD_OS, __BUILD_ARCH);
    return version_info;
}

/**
 * Returns the short version string of the library x.y.z.
 */
const char* __LIB_SHORT_VERSION(void) {
    return __LIB_VERSION_STRING;
}

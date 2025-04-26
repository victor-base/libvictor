/*
* IOFile - Portable File I/O abstraction
*
* Copyright (C) 2025 Emiliano Alejandro Billi
*
* This file is part of the libvictor project.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published
* by the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef IO_FILE_
#define IO_FILE_

#if defined(_WIN32)
#include <stdio.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#endif

/**
 * @brief Represents a portable file object for cross-platform file operations.
 */
typedef struct IOFile {
#if defined(_WIN32)
	FILE *fp; /**< FILE pointer for Windows platforms. */
#else
	int fd;   /**< File descriptor for Unix-like platforms. */
#endif
} IOFile;

/**
 * @brief Opens a file with the specified path and mode ("rb", "wb").
 *
 * @param path Path to the file.
 * @param mode Mode string ("rb" for read binary, "wb" for write binary).
 * @return Pointer to IOFile on success, NULL on failure.
 */
extern IOFile *file_open(const char *path, const char *mode);

/**
 * @brief Reads data from a file into a buffer.
 *
 * @param ptr Destination buffer.
 * @param size Size of each element to read.
 * @param count Number of elements to read.
 * @param f File handle.
 * @return Number of elements successfully read.
 */
extern size_t file_read(void *ptr, size_t size, size_t count, IOFile *f);

/**
 * @brief Writes data from a buffer to a file.
 *
 * @param ptr Source buffer.
 * @param size Size of each element to write.
 * @param count Number of elements to write.
 * @param f File handle.
 * @return Number of elements successfully written.
 */
extern size_t file_write(const void *ptr, size_t size, size_t count, IOFile *f);

/**
 * @brief Moves the file position to a specific offset.
 *
 * @param f File handle.
 * @param offset Offset from the origin specified by whence.
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return 0 on success, -1 on failure.
 */
extern int file_seek(IOFile *f, long offset, int whence);

/**
 * @brief Returns the current offset in the file.
 *
 * @param f File handle.
 * @return Current offset, or (off_t)-1 on failure.
 */
extern off_t file_tello(IOFile *f);

/**
 * @brief Closes the file and releases the IOFile structure.
 *
 * @param f File handle to close.
 */
extern void file_close(IOFile *f);

#endif /* IO_FILE_ */
 

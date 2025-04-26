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

#include "file.h"
#include "mem.h"

#if defined(_WIN32)

/**
 * @brief Opens a file on Windows platforms.
 *
 * @param path Path to the file.
 * @param mode Mode string ("rb", "wb").
 * @return Pointer to IOFile on success, NULL on failure.
 */
IOFile *file_open(const char *path, const char *mode) {
    FILE *fp = fopen(path, mode);
    if (!fp) return NULL;

    IOFile *f = calloc_mem(1, sizeof(IOFile));
    if (!f) return NULL;
    f->fp = fp;
    return f;
}

/**
 * @brief Reads data from a file on Windows.
 *
 * @param ptr Destination buffer.
 * @param size Size of each element.
 * @param count Number of elements to read.
 * @param f File handle.
 * @return Number of elements successfully read.
 */
size_t file_read(void *ptr, size_t size, size_t count, IOFile *f) {
    return fread(ptr, size, count, f->fp);
}

/**
 * @brief Writes data to a file on Windows.
 *
 * @param ptr Source buffer.
 * @param size Size of each element.
 * @param count Number of elements to write.
 * @param f File handle.
 * @return Number of elements successfully written.
 */
size_t file_write(const void *ptr, size_t size, size_t count, IOFile *f) {
    return fwrite(ptr, size, count, f->fp);
}

/**
 * @brief Moves the file position on Windows.
 *
 * @param f File handle.
 * @param offset Offset from the origin specified by whence.
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return 0 on success, -1 on failure.
 */
int file_seek(IOFile *f, long offset, int whence) {
    return fseek(f->fp, offset, whence);
}

/**
 * @brief Returns the current file offset on Windows.
 *
 * @param f File handle.
 * @return Current offset, or (off_t)-1 on failure.
 */
off_t file_tello(IOFile *f) {
    return ftello(f->fp);
}

/**
 * @brief Closes a file and frees resources on Windows.
 *
 * @param f File handle to close.
 */
void file_close(IOFile *f) {
    if (!f) return;
    fclose(f->fp);
    free_mem(f);
}

#else

/**
 * @brief Opens a file on Unix-like platforms.
 *
 * @param path Path to the file.
 * @param mode Mode string ("rb", "wb").
 * @return Pointer to IOFile on success, NULL on failure.
 */
IOFile *file_open(const char *path, const char *mode) {
    int flags = 0;
    if (strcmp(mode, "rb") == 0) flags = O_RDONLY;
    else if (strcmp(mode, "wb") == 0) flags = O_WRONLY | O_CREAT | O_TRUNC;
    else return NULL;

    int fd = open(path, flags, 0644);
    if (fd < 0) return NULL;

    IOFile *f = calloc_mem(1, sizeof(IOFile));
    if (!f) return NULL;
    f->fd = fd;
    return f;
}

/**
 * @brief Reads data from a file on Unix-like platforms.
 *
 * @param ptr Destination buffer.
 * @param size Size of each element.
 * @param count Number of elements to read.
 * @param f File handle.
 * @return Number of elements successfully read.
 */
size_t file_read(void *ptr, size_t size, size_t count, IOFile *f) {
    return read(f->fd, ptr, size * count) / size;
}

/**
 * @brief Writes data to a file on Unix-like platforms.
 *
 * @param ptr Source buffer.
 * @param size Size of each element.
 * @param count Number of elements to write.
 * @param f File handle.
 * @return Number of elements successfully written.
 */
size_t file_write(const void *ptr, size_t size, size_t count, IOFile *f) {
    return write(f->fd, ptr, size * count) / size;
}

/**
 * @brief Moves the file position on Unix-like platforms.
 *
 * @param f File handle.
 * @param offset Offset from the origin specified by whence.
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return 0 on success, -1 on failure.
 */
int file_seek(IOFile *f, long offset, int whence) {
    return lseek(f->fd, offset, whence) == -1 ? -1 : 0;
}

/**
 * @brief Returns the current file offset on Unix-like platforms.
 *
 * @param f File handle.
 * @return Current offset, or (off_t)-1 on failure.
 */
off_t file_tello(IOFile *f) {
    return lseek(f->fd, 0, SEEK_CUR);
}

/**
 * @brief Closes a file and frees resources on Unix-like platforms.
 *
 * @param f File handle to close.
 */
void file_close(IOFile *f) {
    if (!f) return;
    close(f->fd);
    free_mem(f);
}

#endif
 
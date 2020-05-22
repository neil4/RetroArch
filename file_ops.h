/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2015 - Daniel De Matteis
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __LIBRETRO_SDK_FILE_OPS_H
#define __LIBRETRO_SDK_FILE_OPS_H

#include <boolean.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <string/string_list.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_COMPRESSION
/* Generic compressed file loader.
 * Extracts to buf, unless optional_filename != 0
 * Then extracts to optional_filename and leaves buf alone.
 */
int read_compressed_file(const char * path, void **buf,
      const char* optional_filename, ssize_t *length);
#endif

/**
 * read_file:
 * @path             : path to file.
 * @buf              : buffer to allocate and read the contents of the
 *                     file into. Needs to be freed manually.
 * @length           : Number of items read, -1 on error.
 *
 * Read the contents of a file into @buf. Will call read_compressed_file
 * if path contains a compressed file, otherwise will call read_generic_file.
 *
 * Returns: true if file read, false on error.
 */
int read_file(const char *path, void **buf, ssize_t *length);

/**
 * write_file:
 * @path             : path to file.
 * @data             : contents to write to the file.
 * @size             : size of the contents.
 *
 * Writes data to a file.
 *
 * Returns: true (1) on success, false (0) otherwise.
 */
bool write_file(const char *path, const void *buf, ssize_t size);

/**
 * write_rzip_file:
 * @path             : path to file.
 * @data             : contents to compress and write to file.
 * @size             : size of the uncompressed contents.
 *
 * Writes @data to @path in RZIP format.
 *
 * Returns: true on success, false otherwise.
 */
bool write_rzip_file(const char *path, const void *data, uint64_t size);

/**
 * read_rzip_file:
 * @path             : path to file.
 * @buf              : buffer to allocate and read the contents of the
 *                     file into. Needs to be freed manually.
 * @len              : Number of items read. Not updated on failure
 *
 * Decompresses contents from an RZIP file to @buf.
 *
 * Returns: true if file read, false on error.
 */
bool read_rzip_file(const char *path, void **buf, ssize_t *len);

#ifdef __cplusplus
}
#endif

#endif

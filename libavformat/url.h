/*
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * unbuffered private I/O API
 */

#ifndef AVFORMAT_URL_H
#define AVFORMAT_URL_H

#include "avio.h"

/**
 * Create a URLContext for accessing to the resource indicated by
 * url, but do not initiate the connection yet.
 *
 * @param puc pointer to the location where, in case of success, the
 * function puts the pointer to the created URLContext
 * @param flags flags which control how the resource indicated by url
 * is to be opened
 * @return 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure
 */
int ffurl_alloc(URLContext **h, const char *url, int flags);

/**
 * Connect an URLContext that has been allocated by ffurl_alloc
 */
int ffurl_connect(URLContext *h);

/**
 * Create an URLContext for accessing to the resource indicated by
 * url, and open it.
 *
 * @param puc pointer to the location where, in case of success, the
 * function puts the pointer to the created URLContext
 * @param flags flags which control how the resource indicated by url
 * is to be opened
 * @return 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure
 */
int ffurl_open(URLContext **h, const char *url, int flags);

/**
 * Read up to size bytes from the resource accessed by h, and store
 * the read bytes in buf.
 *
 * @return The number of bytes actually read, or a negative value
 * corresponding to an AVERROR code in case of error. A value of zero
 * indicates that it is not possible to read more from the accessed
 * resource (except if the value of the size argument is also zero).
 */
int ffurl_read(URLContext *h, unsigned char *buf, int size);

/**
 * Read as many bytes as possible (up to size), calling the
 * read function multiple times if necessary.
 * This makes special short-read handling in applications
 * unnecessary, if the return value is < size then it is
 * certain there was either an error or the end of file was reached.
 */
int ffurl_read_complete(URLContext *h, unsigned char *buf, int size);

#endif //AVFORMAT_URL_H

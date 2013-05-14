/*
 * Copyright (C) 2013 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVUTIL_HASH_H
#define AVUTIL_HASH_H

#include <stdint.h>

struct AVHashContext;

/**
 * Allocate a hash context for the algorithm specified by name.
 *
 * @return  >= 0 for success, a negative error code for failure
 * @note  The context is not initialized, you must call av_hash_init().
 */
int av_hash_alloc(struct AVHashContext **ctx, const char *name);

/**
 * Get the names of available hash algorithms.
 *
 * This function can be used to enumerate the algorithms.
 *
 * @param i  index of the hash algorithm, starting from 0
 * @return   a pointer to a static string or NULL if i is out of range
 */
const char *av_hash_names(int i);

/**
 * Get the name of the algorithm corresponding to the given hash context.
 */
const char *av_hash_get_name(const struct AVHashContext *ctx);

/**
 * Maximum value that av_hash_get_size will currently return.
 *
 * You can use this if you absolutely want or need to use static allocation
 * and are fine with not supporting hashes newly added to libavutil without
 * recompilation.
 * Note that you still need to check against av_hash_get_size, adding new hashes
 * with larger sizes will not be considered an ABI change and should not cause
 * your code to overflow a buffer.
 */
#define AV_HASH_MAX_SIZE 32

/**
 * Get the size of the resulting hash value in bytes.
 *
 * The pointer passed to av_hash_final have space for at least this many bytes.
 */
int av_hash_get_size(const struct AVHashContext *ctx);

/**
 * Initialize or reset a hash context.
 */
void av_hash_init(struct AVHashContext *ctx);

/**
 * Update a hash context with additional data.
 */
void av_hash_update(struct AVHashContext *ctx, const uint8_t *src, int len);

/**
 * Finalize a hash context and compute the actual hash value.
 */
void av_hash_final(struct AVHashContext *ctx, uint8_t *dst);

/**
 * Free hash context.
 */
void av_hash_freep(struct AVHashContext **ctx);

#endif /* AVUTIL_HASH_H */

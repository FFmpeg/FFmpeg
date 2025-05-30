/*
 * Generic hashtable
 * Copyright (C) 2024 Emma Worley <emma@emma.gg>
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

#ifndef AVCODEC_HASHTABLE_H
#define AVCODEC_HASHTABLE_H

#include <stddef.h>

/* Implements a hash table using Robin Hood open addressing.
 * See: https://cs.uwaterloo.ca/research/tr/1986/CS-86-14.pdf
 *
 * Keys are placed in the table based on their CRC value and are considered
 * equal when they are bytewise-identical.
 */

typedef struct FFHashtableContext FFHashtableContext;

/**
 * Create a fixed-sized Robin Hood hash table.
 *
 * @param ctx         context to allocate and initialize
 * @param key_size    size of key type in bytes
 * @param val_size    size of value type in bytes
 * @param max_entries maximum number of key-value pairs to store
 *
 * @return zero on success, nonzero on error
 */
int ff_hashtable_alloc(struct FFHashtableContext **ctx, size_t key_size, size_t val_size, size_t max_entries);

/**
 * Look up a value from a hash table given a key.
 *
 * @param ctx hash table context
 * @param key pointer to key data
 * @param val destination pointer for value data
 *
 * @return 1 if the key is found, zero if the key is not found
 */
int ff_hashtable_get(const struct FFHashtableContext *ctx, const void *key, void *val);

/**
 * Store a value in a hash table given a key.
 *
 * @param ctx hash table context
 * @param key pointer to key data
 * @param val pointer for value data
 *
 * @return 1 if the key is written, zero if the key is not written due to the hash table reaching max capacity
 */
int ff_hashtable_set(struct FFHashtableContext *ctx, const void *key, const void *val);

/**
 * Delete a value from a hash table given a key.
 *
 * @param ctx hash table context
 * @param key pointer to key data
 *
 * @return 1 if the key is deleted, zero if the key is not deleted due to not being found
 */
int ff_hashtable_delete(struct FFHashtableContext *ctx, const void *key);

/**
 * Delete all values from a hash table.
 *
 * @param ctx hash table context
 */
void ff_hashtable_clear(struct FFHashtableContext *ctx);

/**
 * Free a hash table.
 *
 * @param ctx hash table context
 */
void ff_hashtable_freep(struct FFHashtableContext **ctx);

#endif

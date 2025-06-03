/*
 * Generic hashtable
 * Copyright (C) 2025 Emma Worley <emma@emma.gg>
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

#include <stdint.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/crc.h"
#include "libavutil/error.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "hashtable.h"

#define ALIGN _Alignof(size_t)

struct FFHashtableContext {
    size_t key_size;
    size_t val_size;
    size_t entry_size;
    size_t max_entries;
    size_t nb_entries;
    const AVCRC *crc;
    uint8_t *table;
    uint8_t swapbuf[];
};

/*
 * Hash table entries are comprised of a probe sequence length (PSL), key, and
 * value. When the PSL of an entry is zero, it means it is not occupied by a
 * key/value pair. When the PSL is non-zero, it represents the "distance" of
 * the entry from its "home" location plus one, where the "home" location is
 * hash(key) % max_entries.
 */

#define ENTRY_PSL_VAL(entry) (*(size_t*)(entry))
#define ENTRY_KEY_PTR(entry) ((entry) + sizeof(size_t))
#define ENTRY_VAL_PTR(entry) (ENTRY_KEY_PTR(entry) + ctx->key_size)

#define KEYS_EQUAL(k1, k2) (!memcmp((k1), (k2), ctx->key_size))

av_cold int ff_hashtable_alloc(FFHashtableContext **ctx, size_t key_size,
                               size_t val_size, size_t max_entries)
{
    const size_t keyval_size = key_size + val_size;

    if (keyval_size < key_size || // did (unsigned,defined) wraparound happen?
        keyval_size > FFMIN(SIZE_MAX - sizeof(size_t) - (ALIGN - 1),
                            (SIZE_MAX - sizeof(FFHashtableContext)) / 2))
        return AVERROR(ERANGE);

    FFHashtableContext *res = av_mallocz(sizeof(*res) + 2 * keyval_size);
    if (!res)
        return AVERROR(ENOMEM);
    res->key_size = key_size;
    res->val_size = val_size;
    res->entry_size = FFALIGN(sizeof(size_t) + keyval_size, ALIGN);
    res->max_entries = max_entries;
    res->nb_entries = 0;
    res->crc = av_crc_get_table(AV_CRC_32_IEEE);
    if (!res->crc) {
        ff_hashtable_freep(&res);
        return AVERROR_BUG;
    }
    res->table = av_calloc(res->max_entries, res->entry_size);
    if (!res->table) {
        ff_hashtable_freep(&res);
        return AVERROR(ENOMEM);
    }

    *ctx = res;
    return 0;
}

static size_t hash_key(const struct FFHashtableContext *ctx, const void *key)
{
    return av_crc(ctx->crc, 0, key, ctx->key_size) % ctx->max_entries;
}

int ff_hashtable_get(const struct FFHashtableContext *ctx, const void *key, void *val)
{
    if (!ctx->nb_entries)
        return 0;

    size_t hash = hash_key(ctx, key);

    for (size_t psl = 1; psl <= ctx->max_entries; psl++) {
        size_t wrapped_index = (hash + psl) % ctx->max_entries;
        uint8_t *entry = ctx->table + wrapped_index * ctx->entry_size;
        if (ENTRY_PSL_VAL(entry) < psl)
            // When PSL stops increasing it means there are no further entries
            // with the same key hash.
            return 0;
        if (KEYS_EQUAL(ENTRY_KEY_PTR(entry), key)) {
            memcpy(val, ENTRY_VAL_PTR(entry), ctx->val_size);
            return 1;
        }
    }
    return 0;
}

int ff_hashtable_set(struct FFHashtableContext *ctx, const void *key, const void *val)
{
    int swapping = 0;
    size_t psl = 1;
    size_t hash = hash_key(ctx, key);
    size_t wrapped_index = hash % ctx->max_entries;
    uint8_t *set = ctx->swapbuf;
    uint8_t *tmp = ctx->swapbuf + ctx->key_size + ctx->val_size;

    memcpy(set, key, ctx->key_size);
    memcpy(set + ctx->key_size, val, ctx->val_size);

    for (size_t i = 0; i < ctx->max_entries; i++) {
        if (++wrapped_index == ctx->max_entries)
            wrapped_index = 0;
        uint8_t *entry = ctx->table + wrapped_index * ctx->entry_size;
        if (!ENTRY_PSL_VAL(entry) || (!swapping && KEYS_EQUAL(ENTRY_KEY_PTR(entry), set))) {
            if (!ENTRY_PSL_VAL(entry))
                ctx->nb_entries++;
            ENTRY_PSL_VAL(entry) = psl;
            memcpy(ENTRY_KEY_PTR(entry), set, ctx->key_size + ctx->val_size);
            return 1;
        }
        if (ENTRY_PSL_VAL(entry) < psl) {
            // When PSL stops increasing it means there are no further entries
            // with the same key hash. We can only hope to find an unoccupied
            // entry.
            if (ctx->nb_entries == ctx->max_entries)
                 // The table is full so inserts are impossible.
                return 0;
            // Robin Hood hash tables "steal from the rich" by minimizing the
            // PSL of the inserted entry.
            swapping = 1;
            // set needs to swap with entry
            memcpy(tmp, ENTRY_KEY_PTR(entry), ctx->key_size + ctx->val_size);
            memcpy(ENTRY_KEY_PTR(entry), set, ctx->key_size + ctx->val_size);
            FFSWAP(uint8_t*, set, tmp);
            FFSWAP(size_t, psl, ENTRY_PSL_VAL(entry));
        }
        psl++;
    }
    return 0;
}

int ff_hashtable_delete(struct FFHashtableContext *ctx, const void *key)
{
    if (!ctx->nb_entries)
        return 0;

    uint8_t *next_entry;
    size_t hash = hash_key(ctx, key);
    size_t wrapped_index = hash % ctx->max_entries;

    for (size_t psl = 1; psl <= ctx->max_entries; psl++) {
        if (++wrapped_index == ctx->max_entries)
            wrapped_index = 0;
        uint8_t *entry = ctx->table + wrapped_index * ctx->entry_size;
        if (ENTRY_PSL_VAL(entry) < psl)
            // When PSL stops increasing it means there are no further entries
            // with the same key hash.
            return 0;
        if (KEYS_EQUAL(ENTRY_KEY_PTR(entry), key)) {
            ENTRY_PSL_VAL(entry) = 0;
            // Shift each following entry that will benefit from a reduced PSL.
            for (psl++; psl <= ctx->max_entries; psl++) {
                if (++wrapped_index == ctx->max_entries)
                    wrapped_index = 0;
                next_entry = ctx->table + wrapped_index * ctx->entry_size;
                if (ENTRY_PSL_VAL(next_entry) <= 1) {
                    ctx->nb_entries--;
                    return 1;
                }
                memcpy(entry, next_entry, ctx->entry_size);
                ENTRY_PSL_VAL(entry)--;
                ENTRY_PSL_VAL(next_entry) = 0;
                entry = next_entry;
            }
        }
    }
    return 0;
}

void ff_hashtable_clear(struct FFHashtableContext *ctx)
{
    memset(ctx->table, 0, ctx->entry_size * ctx->max_entries);
}

av_cold void ff_hashtable_freep(FFHashtableContext **ctx)
{
    if (*ctx) {
        av_freep(&(*ctx)->table);
        av_freep(ctx);
    }
}

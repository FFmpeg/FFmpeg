/*
 * Generic hashtable tests
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

#include <stdint.h>

#include "libavutil/avassert.h"
#include "libavcodec/hashtable.h"

int main(void)
{
    struct FFHashtableContext *ctx;
    uint8_t k;
    uint64_t v;

    // impossibly large allocation should fail gracefully
    av_assert0(ff_hashtable_alloc(&ctx, -1, -1, -1) < 0);

    // hashtable can store up to 3 uint8_t->uint64_t entries
    av_assert0(!ff_hashtable_alloc(&ctx, sizeof(k), sizeof(v), 3));

    // unsuccessful deletes return 0
    k = 1;
    av_assert0(!ff_hashtable_delete(ctx, &k));

    // unsuccessful gets return 0
    k = 1;
    av_assert0(!ff_hashtable_get(ctx, &k, &v));

    // successful sets returns 1
    k = 1;
    v = 1;
    av_assert0(ff_hashtable_set(ctx, &k, &v));

    // get should now contain 1
    k = 1;
    v = 0;
    av_assert0(ff_hashtable_get(ctx, &k, &v));
    av_assert0(v == 1);

    // updating sets should return 1
    k = 1;
    v = 2;
    av_assert0(ff_hashtable_set(ctx, &k, &v));

    // get should now contain 2
    k = 1;
    v = 0;
    av_assert0(ff_hashtable_get(ctx, &k, &v));
    av_assert0(v == 2);

    // fill the table
    k = 2;
    v = 2;
    av_assert0(ff_hashtable_set(ctx, &k, &v));
    k = 3;
    v = 3;
    av_assert0(ff_hashtable_set(ctx, &k, &v));

    // inserting sets on a full table should return 0
    k = 4;
    v = 4;
    av_assert0(!ff_hashtable_set(ctx, &k, &v));

    // updating sets on a full table should return 1
    k = 1;
    v = 4;
    av_assert0(ff_hashtable_set(ctx, &k, &v));
    v = 0;
    av_assert0(ff_hashtable_get(ctx, &k, &v));
    av_assert0(v == 4);

    // successful deletes should return 1
    k = 1;
    av_assert0(ff_hashtable_delete(ctx, &k));

    // get should now return 0
    av_assert0(!ff_hashtable_get(ctx, &k, &v));

    // sanity check remaining keys
    k = 2;
    v = 0;
    av_assert0(ff_hashtable_get(ctx, &k, &v));
    av_assert0(v == 2);
    k = 3;
    v = 0;
    av_assert0(ff_hashtable_get(ctx, &k, &v));
    av_assert0(v == 3);

    ff_hashtable_freep(&ctx);

    return 0;
}

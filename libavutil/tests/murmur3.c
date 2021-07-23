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

#include <inttypes.h>
#include <stdio.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/murmur3.h"

int main(void)
{
    int i;
    uint8_t hash_result[16] = {0};
    struct AVMurMur3 *ctx = av_murmur3_alloc();
#if 1
    uint8_t in[256] = {0};
    uint8_t *hashes = av_mallocz(256 * 16);
    for (i = 0; i < 256; i++)
    {
        in[i] = i;
        av_murmur3_init_seeded(ctx, 256 - i);
        // Note: this actually tests hashing 0 bytes
        av_murmur3_update(ctx, in, i);
        av_murmur3_final(ctx, hashes + 16 * i);
    }
    av_murmur3_init_seeded(ctx, 0);
    av_murmur3_update(ctx, hashes, 256 * 16);
    av_murmur3_final(ctx, hash_result);
    av_free(hashes);
    av_freep(&ctx);
    printf("result: 0x%"PRIx64" 0x%"PRIx64"\n", AV_RL64(hash_result), AV_RL64(hash_result + 8));
    // official reference value is 32 bit
    return AV_RL32(hash_result) != 0x6384ba69;
#else
    uint8_t *in = av_mallocz(512*1024);
    av_murmur3_init(ctx);
    for (i = 0; i < 40*1024; i++)
        av_murmur3_update(ctx, in, 512*1024);
    av_murmur3_final(ctx, hash_result);
    av_free(in);
    return hash_result[0];
#endif
}

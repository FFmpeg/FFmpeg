/*
 * Lagged Fibonacci PRNG
 * Copyright (c) 2008 Michael Niedermayer
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
#include <limits.h>
#include <math.h>
#include "lfg.h"
#include "crc.h"
#include "md5.h"
#include "error.h"
#include "intreadwrite.h"
#include "attributes.h"

av_cold void av_lfg_init(AVLFG *c, unsigned int seed)
{
    uint8_t tmp[16] = { 0 };
    int i;

    for (i = 8; i < 64; i += 4) {
        AV_WL32(tmp, seed);
        tmp[4] = i;
        av_md5_sum(tmp, tmp, 16);
        c->state[i    ] = AV_RL32(tmp);
        c->state[i + 1] = AV_RL32(tmp + 4);
        c->state[i + 2] = AV_RL32(tmp + 8);
        c->state[i + 3] = AV_RL32(tmp + 12);
    }
    c->index = 0;
}

void av_bmg_get(AVLFG *lfg, double out[2])
{
    double x1, x2, w;

    do {
        x1 = 2.0 / UINT_MAX * av_lfg_get(lfg) - 1.0;
        x2 = 2.0 / UINT_MAX * av_lfg_get(lfg) - 1.0;
        w  = x1 * x1 + x2 * x2;
    } while (w >= 1.0);

    w = sqrt((-2.0 * log(w)) / w);
    out[0] = x1 * w;
    out[1] = x2 * w;
}

int av_lfg_init_from_data(AVLFG *c, const uint8_t *data, unsigned int length) {
    unsigned int beg, end, segm;
    const AVCRC *avcrc;
    uint32_t crc = 1;

    /* avoid integer overflow in the loop below. */
    if (length > (UINT_MAX / 128U)) return AVERROR(EINVAL);

    c->index = 0;
    avcrc = av_crc_get_table(AV_CRC_32_IEEE); /* This can't fail. It's a well-defined table in crc.c */

    /* across 64 segments of the incoming data,
     * do a running crc of each segment and store the crc as the state for that slot.
     * this works even if the length of the segment is 0 bytes. */
    beg = 0;
    for (segm = 0;segm < 64;segm++) {
        end = (((segm + 1) * length) / 64);
        crc = av_crc(avcrc, crc, data + beg, end - beg);
        c->state[segm] = (unsigned int)crc;
        beg = end;
    }

    return 0;
}

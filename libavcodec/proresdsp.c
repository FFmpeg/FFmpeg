/*
 * Apple ProRes compatible decoder
 *
 * Copyright (c) 2010-2011 Maxim Poliakovski
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

#include "proresdsp.h"
#include "simple_idct.h"

#define BIAS     (1 << (PRORES_BITS_PER_SAMPLE - 1))           ///< bias value for converting signed pixels into unsigned ones
#define CLIP_MIN (1 << (PRORES_BITS_PER_SAMPLE - 8))           ///< minimum value for clipping resulting pixels
#define CLIP_MAX (1 << PRORES_BITS_PER_SAMPLE) - CLIP_MIN - 1  ///< maximum value for clipping resulting pixels

#define CLIP_AND_BIAS(x) (av_clip((x) + BIAS, CLIP_MIN, CLIP_MAX))

/**
 * Add bias value, clamp and output pixels of a slice
 */
static void put_pixels(uint16_t *dst, int stride, const DCTELEM *in)
{
    int x, y, src_offset, dst_offset;

    for (y = 0, dst_offset = 0; y < 8; y++, dst_offset += stride) {
        for (x = 0; x < 8; x++) {
            src_offset = (y << 3) + x;

            dst[dst_offset + x] = CLIP_AND_BIAS(in[src_offset]);
        }
    }
}

static void prores_idct_put_c(uint16_t *out, int linesize, DCTELEM *block, const int16_t *qmat)
{
    ff_prores_idct(block, qmat);
    put_pixels(out, linesize >> 1, block);
}

void ff_proresdsp_init(ProresDSPContext *dsp)
{
    dsp->idct_put = prores_idct_put_c;
    dsp->idct_permutation_type = FF_NO_IDCT_PERM;

    if (HAVE_MMX) ff_proresdsp_x86_init(dsp);

    ff_init_scantable_permutation(dsp->idct_permutation,
                                  dsp->idct_permutation_type);
}

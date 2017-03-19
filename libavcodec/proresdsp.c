/*
 * Apple ProRes compatible decoder
 *
 * Copyright (c) 2010-2011 Maxim Poliakovski
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "idctdsp.h"
#include "proresdsp.h"
#include "simple_idct.h"

#define BIAS     (1 << (PRORES_BITS_PER_SAMPLE - 1))           ///< bias value for converting signed pixels into unsigned ones
#define CLIP_MIN (1 << (PRORES_BITS_PER_SAMPLE - 8))           ///< minimum value for clipping resulting pixels
#define CLIP_MAX (1 << PRORES_BITS_PER_SAMPLE) - CLIP_MIN - 1  ///< maximum value for clipping resulting pixels

#define CLIP(x) (av_clip((x), CLIP_MIN, CLIP_MAX))

/**
 * Add bias value, clamp and output pixels of a slice
 */
static void put_pixels(uint16_t *dst, ptrdiff_t linesize, const int16_t *in)
{
    int x, y, src_offset, dst_offset;

    for (y = 0, dst_offset = 0; y < 8; y++, dst_offset += linesize) {
        for (x = 0; x < 8; x++) {
            src_offset = (y << 3) + x;

            dst[dst_offset + x] = CLIP(in[src_offset]);
        }
    }
}

static void prores_idct_put_c(uint16_t *out, ptrdiff_t linesize, int16_t *block, const int16_t *qmat)
{
    ff_prores_idct(block, qmat);
    put_pixels(out, linesize >> 1, block);
}

av_cold void ff_proresdsp_init(ProresDSPContext *dsp, AVCodecContext *avctx)
{
    dsp->idct_put = prores_idct_put_c;
    dsp->idct_permutation_type = FF_IDCT_PERM_NONE;

    if (ARCH_X86)
        ff_proresdsp_init_x86(dsp, avctx);

    ff_init_scantable_permutation(dsp->idct_permutation,
                                  dsp->idct_permutation_type);
}

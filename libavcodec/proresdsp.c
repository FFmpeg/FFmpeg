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

#define CLIP_MIN (1 << 2)                     ///< minimum value for clipping resulting pixels
#define CLIP_MAX_10 (1 << 10) - CLIP_MIN - 1  ///< maximum value for clipping resulting pixels
#define CLIP_MAX_12 (1 << 12) - CLIP_MIN - 1  ///< maximum value for clipping resulting pixels

#define CLIP_10(x) (av_clip((x), CLIP_MIN, CLIP_MAX_10))
#define CLIP_12(x) (av_clip((x), CLIP_MIN, CLIP_MAX_12))

/**
 * Add bias value, clamp and output pixels of a slice
 */

static inline void put_pixel(uint16_t *dst, ptrdiff_t linesize, const int16_t *in, int bits_per_raw_sample) {
    int x, y, src_offset, dst_offset;

    for (y = 0, dst_offset = 0; y < 8; y++, dst_offset += linesize) {
        for (x = 0; x < 8; x++) {
            src_offset = (y << 3) + x;

            if (bits_per_raw_sample == 10) {
                dst[dst_offset + x] = CLIP_10(in[src_offset]);
            } else {//12b
                dst[dst_offset + x] = CLIP_12(in[src_offset]);
            }
        }
    }
}

static void put_pixels_10(uint16_t *dst, ptrdiff_t linesize, const int16_t *in)
{
    put_pixel(dst, linesize, in, 10);
}

static void put_pixels_12(uint16_t *dst, ptrdiff_t linesize, const int16_t *in)
{
    put_pixel(dst, linesize, in, 12);
}

static void prores_idct_put_10_c(uint16_t *out, ptrdiff_t linesize, int16_t *block, const int16_t *qmat)
{
    ff_prores_idct_10(block, qmat);
    put_pixels_10(out, linesize >> 1, block);
}

static void prores_idct_put_12_c(uint16_t *out, ptrdiff_t linesize, int16_t *block, const int16_t *qmat)
{
    ff_prores_idct_12(block, qmat);
    put_pixels_12(out, linesize >> 1, block);
}

av_cold int ff_proresdsp_init(ProresDSPContext *dsp, AVCodecContext *avctx)
{
    if (avctx->bits_per_raw_sample == 10) {
        dsp->idct_put = prores_idct_put_10_c;
        dsp->idct_permutation_type = FF_IDCT_PERM_NONE;
    } else if (avctx->bits_per_raw_sample == 12) {
        dsp->idct_put = prores_idct_put_12_c;
        dsp->idct_permutation_type = FF_IDCT_PERM_NONE;
    } else {
        return AVERROR_BUG;
    }

    if (ARCH_X86)
        ff_proresdsp_init_x86(dsp, avctx);

    ff_init_scantable_permutation(dsp->idct_permutation,
                                  dsp->idct_permutation_type);
    return 0;
}

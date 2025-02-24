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
#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "idctdsp.h"
#include "proresdsp.h"

#define IN_IDCT_DEPTH 16
#define PRORES_ONLY

#define BIT_DEPTH 10
#define EXTRA_SHIFT
#include "simple_idct_template.c"
#undef BIT_DEPTH
#undef EXTRA_SHIFT

#define BIT_DEPTH 12
#include "simple_idct_template.c"
#undef BIT_DEPTH

/**
 * Special version of ff_simple_idct_int16_10bit() which does dequantization
 * and scales by a factor of 2 more between the two IDCTs to account
 * for larger scale of input coefficients.
 */
static void prores_idct_10(int16_t *restrict block, const int16_t *restrict qmat)
{
    for (int i = 0; i < 64; i++)
        block[i] *= qmat[i];

    for (int i = 0; i < 8; i++)
        idctRowCondDC_extrashift_10(block + i*8, 2);

    for (int i = 0; i < 8; i++) {
        block[i] += 8192;
        idctSparseCol_extrashift_10(block + i);
    }
}

static void prores_idct_12(int16_t *restrict block, const int16_t *restrict qmat)
{
    for (int i = 0; i < 64; i++)
        block[i] *= qmat[i];

    for (int i = 0; i < 8; i++)
        idctRowCondDC_int16_12bit(block + i*8, 0);

    for (int i = 0; i < 8; i++) {
        block[i] += 8192;
        idctSparseCol_int16_12bit(block + i);
    }
}

#define CLIP_MIN (1 << 2)                     ///< minimum value for clipping resulting pixels
#define CLIP_MAX_10 (1 << 10) - CLIP_MIN - 1  ///< maximum value for clipping resulting pixels
#define CLIP_MAX_12 (1 << 12) - CLIP_MIN - 1  ///< maximum value for clipping resulting pixels

#define CLIP_10(x) (av_clip((x), CLIP_MIN, CLIP_MAX_10))
#define CLIP_12(x) (av_clip((x), CLIP_MIN, CLIP_MAX_12))

/**
 * Add bias value, clamp and output pixels of a slice
 */

static inline void put_pixel(uint16_t *dst, ptrdiff_t linesize, const int16_t *in, int bits_per_raw_sample) {
    for (int y = 0; y < 8; y++, dst += linesize) {
        for (int x = 0; x < 8; x++) {
            int src_offset = (y << 3) + x;

            if (bits_per_raw_sample == 10) {
                dst[x] = CLIP_10(in[src_offset]);
            } else {//12b
                dst[x] = CLIP_12(in[src_offset]);
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
    prores_idct_10(block, qmat);
    put_pixels_10(out, linesize >> 1, block);
}

static void prores_idct_put_12_c(uint16_t *out, ptrdiff_t linesize, int16_t *block, const int16_t *qmat)
{
    prores_idct_12(block, qmat);
    put_pixels_12(out, linesize >> 1, block);
}

av_cold void ff_proresdsp_init(ProresDSPContext *dsp, int bits_per_raw_sample)
{
    if (bits_per_raw_sample == 10) {
        dsp->idct_put = prores_idct_put_10_c;
        dsp->idct_permutation_type = FF_IDCT_PERM_NONE;
    } else {
        av_assert1(bits_per_raw_sample == 12);
        dsp->idct_put = prores_idct_put_12_c;
        dsp->idct_permutation_type = FF_IDCT_PERM_NONE;
    }

#if ARCH_X86
    ff_proresdsp_init_x86(dsp, bits_per_raw_sample);
#endif

    ff_init_scantable_permutation(dsp->idct_permutation,
                                  dsp->idct_permutation_type);
}

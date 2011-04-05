/*
 * AC-3 DSP utils
 * Copyright (c) 2011 Justin Ruggles
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

#include "libavutil/avassert.h"
#include "avcodec.h"
#include "ac3.h"
#include "ac3dsp.h"

static void ac3_exponent_min_c(uint8_t *exp, int num_reuse_blocks, int nb_coefs)
{
    int blk, i;

    if (!num_reuse_blocks)
        return;

    for (i = 0; i < nb_coefs; i++) {
        uint8_t min_exp = *exp;
        uint8_t *exp1 = exp + 256;
        for (blk = 0; blk < num_reuse_blocks; blk++) {
            uint8_t next_exp = *exp1;
            if (next_exp < min_exp)
                min_exp = next_exp;
            exp1 += 256;
        }
        *exp++ = min_exp;
    }
}

static int ac3_max_msb_abs_int16_c(const int16_t *src, int len)
{
    int i, v = 0;
    for (i = 0; i < len; i++)
        v |= abs(src[i]);
    return v;
}

static void ac3_lshift_int16_c(int16_t *src, unsigned int len,
                               unsigned int shift)
{
    uint32_t *src32 = (uint32_t *)src;
    const uint32_t mask = ~(((1 << shift) - 1) << 16);
    int i;
    len >>= 1;
    for (i = 0; i < len; i += 8) {
        src32[i  ] = (src32[i  ] << shift) & mask;
        src32[i+1] = (src32[i+1] << shift) & mask;
        src32[i+2] = (src32[i+2] << shift) & mask;
        src32[i+3] = (src32[i+3] << shift) & mask;
        src32[i+4] = (src32[i+4] << shift) & mask;
        src32[i+5] = (src32[i+5] << shift) & mask;
        src32[i+6] = (src32[i+6] << shift) & mask;
        src32[i+7] = (src32[i+7] << shift) & mask;
    }
}

static void ac3_rshift_int32_c(int32_t *src, unsigned int len,
                               unsigned int shift)
{
    do {
        *src++ >>= shift;
        *src++ >>= shift;
        *src++ >>= shift;
        *src++ >>= shift;
        *src++ >>= shift;
        *src++ >>= shift;
        *src++ >>= shift;
        *src++ >>= shift;
        len -= 8;
    } while (len > 0);
}

static void float_to_fixed24_c(int32_t *dst, const float *src, unsigned int len)
{
    const float scale = 1 << 24;
    do {
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        len -= 8;
    } while (len > 0);
}

static void ac3_bit_alloc_calc_bap_c(int16_t *mask, int16_t *psd,
                                     int start, int end,
                                     int snr_offset, int floor,
                                     const uint8_t *bap_tab, uint8_t *bap)
{
    int bin, band;

    /* special case, if snr offset is -960, set all bap's to zero */
    if (snr_offset == -960) {
        memset(bap, 0, AC3_MAX_COEFS);
        return;
    }

    bin  = start;
    band = ff_ac3_bin_to_band_tab[start];
    do {
        int m = (FFMAX(mask[band] - snr_offset - floor, 0) & 0x1FE0) + floor;
        int band_end = FFMIN(ff_ac3_band_start_tab[band+1], end);
        for (; bin < band_end; bin++) {
            int address = av_clip((psd[bin] - m) >> 5, 0, 63);
            bap[bin] = bap_tab[address];
        }
    } while (end > ff_ac3_band_start_tab[band++]);
}

static int ac3_compute_mantissa_size_c(int mant_cnt[5], uint8_t *bap,
                                       int nb_coefs)
{
    int bits, b, i;

    bits = 0;
    for (i = 0; i < nb_coefs; i++) {
        b = bap[i];
        if (b <= 4) {
            // bap=1 to bap=4 will be counted in compute_mantissa_size_final
            mant_cnt[b]++;
        } else if (b <= 13) {
            // bap=5 to bap=13 use (bap-1) bits
            bits += b - 1;
        } else {
            // bap=14 uses 14 bits and bap=15 uses 16 bits
            bits += (b == 14) ? 14 : 16;
        }
    }
    return bits;
}

static void ac3_extract_exponents_c(uint8_t *exp, int32_t *coef, int nb_coefs)
{
    int i;

    for (i = 0; i < nb_coefs; i++) {
        int e;
        int v = abs(coef[i]);
        if (v == 0)
            e = 24;
        else {
            e = 23 - av_log2(v);
            if (e >= 24) {
                e = 24;
                coef[i] = 0;
            }
            av_assert2(e >= 0);
        }
        exp[i] = e;
    }
}

av_cold void ff_ac3dsp_init(AC3DSPContext *c, int bit_exact)
{
    c->ac3_exponent_min = ac3_exponent_min_c;
    c->ac3_max_msb_abs_int16 = ac3_max_msb_abs_int16_c;
    c->ac3_lshift_int16 = ac3_lshift_int16_c;
    c->ac3_rshift_int32 = ac3_rshift_int32_c;
    c->float_to_fixed24 = float_to_fixed24_c;
    c->bit_alloc_calc_bap = ac3_bit_alloc_calc_bap_c;
    c->compute_mantissa_size = ac3_compute_mantissa_size_c;
    c->extract_exponents = ac3_extract_exponents_c;

    if (ARCH_ARM)
        ff_ac3dsp_init_arm(c, bit_exact);
    if (HAVE_MMX)
        ff_ac3dsp_init_x86(c, bit_exact);
}

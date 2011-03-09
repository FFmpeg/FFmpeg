/*
 * AC-3 DSP utils
 * Copyright (c) 2011 Justin Ruggles
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

#include "avcodec.h"
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

av_cold void ff_ac3dsp_init(AC3DSPContext *c, int bit_exact)
{
    c->ac3_exponent_min = ac3_exponent_min_c;
    c->ac3_max_msb_abs_int16 = ac3_max_msb_abs_int16_c;
    c->ac3_lshift_int16 = ac3_lshift_int16_c;
    c->ac3_rshift_int32 = ac3_rshift_int32_c;
    c->float_to_fixed24 = float_to_fixed24_c;

    if (ARCH_ARM)
        ff_ac3dsp_init_arm(c, bit_exact);
    if (HAVE_MMX)
        ff_ac3dsp_init_x86(c, bit_exact);
}

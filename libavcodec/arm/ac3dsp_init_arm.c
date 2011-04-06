/*
 * Copyright (c) 2011 Mans Rullgard <mans@mansr.com>
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
#include "libavutil/attributes.h"
#include "libavcodec/ac3dsp.h"
#include "config.h"

void ff_ac3_exponent_min_neon(uint8_t *exp, int num_reuse_blocks, int nb_coefs);
int ff_ac3_max_msb_abs_int16_neon(const int16_t *src, int len);
void ff_ac3_lshift_int16_neon(int16_t *src, unsigned len, unsigned shift);
void ff_ac3_rshift_int32_neon(int32_t *src, unsigned len, unsigned shift);
void ff_float_to_fixed24_neon(int32_t *dst, const float *src, unsigned int len);
void ff_ac3_extract_exponents_neon(uint8_t *exp, int32_t *coef, int nb_coefs);

void ff_ac3_bit_alloc_calc_bap_armv6(int16_t *mask, int16_t *psd,
                                     int start, int end,
                                     int snr_offset, int floor,
                                     const uint8_t *bap_tab, uint8_t *bap);

int ff_ac3_compute_mantissa_size_arm(int cnt[5], uint8_t *bap, int nb_coefs);

av_cold void ff_ac3dsp_init_arm(AC3DSPContext *c, int bit_exact)
{
    c->compute_mantissa_size     = ff_ac3_compute_mantissa_size_arm;

    if (HAVE_ARMV6) {
        c->bit_alloc_calc_bap    = ff_ac3_bit_alloc_calc_bap_armv6;
    }

    if (HAVE_NEON) {
        c->ac3_exponent_min      = ff_ac3_exponent_min_neon;
        c->ac3_max_msb_abs_int16 = ff_ac3_max_msb_abs_int16_neon;
        c->ac3_lshift_int16      = ff_ac3_lshift_int16_neon;
        c->ac3_rshift_int32      = ff_ac3_rshift_int32_neon;
        c->float_to_fixed24      = ff_float_to_fixed24_neon;
        c->extract_exponents     = ff_ac3_extract_exponents_neon;
    }
}

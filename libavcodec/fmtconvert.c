/*
 * Format Conversion Utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
#include "fmtconvert.h"
#include "libavutil/common.h"

static void int32_to_float_fmul_scalar_c(float *dst, const int32_t *src,
                                         float mul, int len)
{
    int i;
    for(i=0; i<len; i++)
        dst[i] = src[i] * mul;
}

static void int32_to_float_c(float *dst, const int32_t *src, intptr_t len)
{
    int i;

    for (i = 0; i < len; i++)
        dst[i] = (float)src[i];
}

static void int32_to_float_fmul_array8_c(FmtConvertContext *c, float *dst,
                                         const int32_t *src, const float *mul,
                                         int len)
{
    int i;
    for (i = 0; i < len; i += 8)
        c->int32_to_float_fmul_scalar(&dst[i], &src[i], *mul++, 8);
}

av_cold void ff_fmt_convert_init(FmtConvertContext *c, AVCodecContext *avctx)
{
    c->int32_to_float             = int32_to_float_c;
    c->int32_to_float_fmul_scalar = int32_to_float_fmul_scalar_c;
    c->int32_to_float_fmul_array8 = int32_to_float_fmul_array8_c;

    if (ARCH_AARCH64)
        ff_fmt_convert_init_aarch64(c, avctx);
    if (ARCH_ARM)
        ff_fmt_convert_init_arm(c, avctx);
    if (ARCH_PPC)
        ff_fmt_convert_init_ppc(c, avctx);
    if (ARCH_X86)
        ff_fmt_convert_init_x86(c, avctx);
}

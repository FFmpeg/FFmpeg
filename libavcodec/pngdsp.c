/*
 * PNG image format
 * Copyright (c) 2008 Loren Merrit <lorenm@u.washington.edu>
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
#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"
#include "png.h"
#include "pngdsp.h"

#if HAVE_FAST_64BIT
#define BITS 64
typedef uint64_t uint_native;
#else
#define BITS 32
typedef uint32_t uint_native;
#endif
#define RN  AV_JOIN(AV_RN, BITS)
#define RNA AV_JOIN(AV_JOIN(AV_RN, BITS),  A)
#define WN  AV_JOIN(AV_WN, BITS)

// 0x7f7f7f7f or 0x7f7f7f7f7f7f7f7f or whatever, depending on the cpu's native arithmetic size
#define pb_7f (~(uint_native)0 / 255 * 0x7f)
#define pb_80 (~(uint_native)0 / 255 * 0x80)

static void add_bytes_l2_c(uint8_t *dst, uint8_t *src1, uint8_t *src2, int w)
{
    long i;
    for (i = 0; i <= w - (int) sizeof(uint_native); i += sizeof(uint_native)) {
        uint_native a = RNA(src1 + i);
        uint_native b = RN (src2 + i);
        WN(dst + i, ((a & pb_7f) + (b & pb_7f)) ^ ((a ^ b) & pb_80));
    }
    for (; i < w; i++)
        dst[i] = src1[i] + src2[i];
}

av_cold void ff_pngdsp_init(PNGDSPContext *dsp)
{
    dsp->add_bytes_l2         = add_bytes_l2_c;
    dsp->add_paeth_prediction = ff_add_png_paeth_prediction;

#if ARCH_X86
    ff_pngdsp_init_x86(dsp);
#endif
}

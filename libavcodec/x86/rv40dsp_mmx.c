/*
 * Copyright (c) 2008 Konstantin Shishkov, Mathieu Velten
 *
 * MMX-optimized DSP functions for RV40, based on H.264 optimizations by
 * Michael Niedermayer and Loren Merritt
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

#include "dsputil_mmx.h"

/* bias interleaved with bias div 8, use p+1 to access bias div 8 */
DECLARE_ALIGNED_8(static const uint64_t, rv40_bias_reg[4][8]) = {
    { 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0010001000100010ULL, 0x0002000200020002ULL,
      0x0020002000200020ULL, 0x0004000400040004ULL, 0x0010001000100010ULL, 0x0002000200020002ULL },
    { 0x0020002000200020ULL, 0x0004000400040004ULL, 0x001C001C001C001CULL, 0x0003000300030003ULL,
      0x0020002000200020ULL, 0x0004000400040004ULL, 0x001C001C001C001CULL, 0x0003000300030003ULL },
    { 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0020002000200020ULL, 0x0004000400040004ULL,
      0x0010001000100010ULL, 0x0002000200020002ULL, 0x0020002000200020ULL, 0x0004000400040004ULL },
    { 0x0020002000200020ULL, 0x0004000400040004ULL, 0x001C001C001C001CULL, 0x0003000300030003ULL,
      0x0020002000200020ULL, 0x0004000400040004ULL, 0x001C001C001C001CULL, 0x0003000300030003ULL }
};

static void put_rv40_chroma_mc8_mmx(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    put_h264_chroma_generic_mc8_mmx(dst, src, stride, h, x, y, &rv40_bias_reg[y>>1][x&(~1)]);
}
static void put_rv40_chroma_mc4_mmx(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    put_h264_chroma_generic_mc4_mmx(dst, src, stride, h, x, y, &rv40_bias_reg[y>>1][x&(~1)]);
}
static void avg_rv40_chroma_mc8_mmx2(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    avg_h264_chroma_generic_mc8_mmx2(dst, src, stride, h, x, y, &rv40_bias_reg[y>>1][x&(~1)]);
}
static void avg_rv40_chroma_mc4_mmx2(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    avg_h264_chroma_generic_mc4_mmx2(dst, src, stride, h, x, y, &rv40_bias_reg[y>>1][x&(~1)]);
}
static void avg_rv40_chroma_mc8_3dnow(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    avg_h264_chroma_generic_mc8_3dnow(dst, src, stride, h, x, y, &rv40_bias_reg[y>>1][x&(~1)]);
}
static void avg_rv40_chroma_mc4_3dnow(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    avg_h264_chroma_generic_mc4_3dnow(dst, src, stride, h, x, y, &rv40_bias_reg[y>>1][x&(~1)]);
}

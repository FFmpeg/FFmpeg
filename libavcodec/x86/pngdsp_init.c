/*
 * x86 PNG optimizations.
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

#include "libavutil/common.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/pngdsp.h"

void ff_add_png_paeth_prediction_mmx2 (uint8_t *dst, uint8_t *src,
                                       uint8_t *top, int w, int bpp);
void ff_add_png_paeth_prediction_ssse3(uint8_t *dst, uint8_t *src,
                                       uint8_t *top, int w, int bpp);
void ff_add_bytes_l2_mmx (uint8_t *dst, uint8_t *src1,
                          uint8_t *src2, int w);
void ff_add_bytes_l2_sse2(uint8_t *dst, uint8_t *src1,
                          uint8_t *src2, int w);

void ff_pngdsp_init_x86(PNGDSPContext *dsp)
{
    int flags = av_get_cpu_flags();

#if ARCH_X86_32
    if (EXTERNAL_MMX(flags))
        dsp->add_bytes_l2         = ff_add_bytes_l2_mmx;
#endif
    if (EXTERNAL_MMXEXT(flags))
        dsp->add_paeth_prediction = ff_add_png_paeth_prediction_mmx2;
    if (EXTERNAL_SSE2(flags))
        dsp->add_bytes_l2         = ff_add_bytes_l2_sse2;
    if (EXTERNAL_SSSE3(flags))
        dsp->add_paeth_prediction = ff_add_png_paeth_prediction_ssse3;
}

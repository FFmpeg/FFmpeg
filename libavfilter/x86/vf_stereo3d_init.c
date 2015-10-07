/*
 * Copyright (c) 2015 Paul B Mahol
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

#include "libavutil/x86/cpu.h"

#include "libavfilter/stereo3d.h"

void ff_anaglyph_sse4(uint8_t *dst, uint8_t *lsrc, uint8_t *rsrc,
                      ptrdiff_t dst_linesize, ptrdiff_t l_linesize, ptrdiff_t r_linesize,
                      int width, int height,
                      const int *ana_matrix_r, const int *ana_matrix_g, const int *ana_matrix_b);

void ff_stereo3d_init_x86(Stereo3DDSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (ARCH_X86_64 && EXTERNAL_SSE4(cpu_flags)) {
        dsp->anaglyph = ff_anaglyph_sse4;
    }
}
